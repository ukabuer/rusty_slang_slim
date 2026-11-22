import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import zipfile


SPEC = importlib.util.spec_from_file_location(
    "repackage_native", Path(__file__).resolve().parents[1] / "repackage-native.py"
)
repackager = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(repackager)


class NativeRepackagingTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.source, self.output = self.root / "old.zip", self.root / "new.zip"
        self.payload = {"lib/slang-slim.lib": b"unchanged native library\x00\xff",
                        "include/slang_c_api.h": b"// native header\n"}
        self.manifest = {
            "schema_version": 1, "abi_version": 1, "package": "slang-slim-native",
            "version": "0.1.1", "target": "x86_64-pc-windows-msvc", "slang_commit": "pinned",
            "link": {"kind": "static", "libraries": [{"name": "slang-slim", "path": "lib/slang-slim.lib"}]},
            "files": [{"path": name, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()}
                      for name, data in self.payload.items()],
        }
        self.write_source()

    def write_source(self, payload=None):
        with zipfile.ZipFile(self.source, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            for name, data in (payload or self.payload).items():
                archive.writestr(name, data)
            archive.writestr("manifest.json", json.dumps(self.manifest))

    def repackage(self, source_version="0.1.1", target="x86_64-pc-windows-msvc"):
        repackager.repackage(self.source, self.output, source_version, "0.1.2", target)

    def test_native_payload_and_its_checksums_remain_identical(self):
        original_archive = self.source.read_bytes()
        self.repackage()
        with zipfile.ZipFile(self.output) as archive:
            for name, data in self.payload.items():
                self.assertEqual(archive.read(name), data)
            manifest = json.loads(archive.read("manifest.json"))
        self.assertEqual(manifest["version"], "0.1.2")
        self.assertEqual(manifest["binary_source_version"], "0.1.1")
        for field in ("files", "link", "slang_commit", "abi_version"):
            self.assertEqual(manifest[field], self.manifest[field])
        self.assertEqual(self.source.read_bytes(), original_archive)

    def test_successive_repackaging_retains_original_binary_provenance(self):
        self.repackage()
        repackager.repackage(self.output, self.root / "third.zip", "0.1.2", "0.1.3", "x86_64-pc-windows-msvc")
        with zipfile.ZipFile(self.root / "third.zip") as archive:
            self.assertEqual(json.loads(archive.read("manifest.json"))["binary_source_version"], "0.1.1")

    def test_corrupt_payload_does_not_replace_an_existing_output(self):
        self.write_source(dict(self.payload, **{"lib/slang-slim.lib": b"corrupt"}))
        self.output.write_bytes(b"existing archive")
        with self.assertRaisesRegex(ValueError, "checksum or size mismatch"):
            self.repackage()
        self.assertEqual(self.output.read_bytes(), b"existing archive")
        self.assertFalse(list(self.root.glob("*.tmp")))

    def test_source_version_and_target_are_checked(self):
        for source_version, target in (("0.1.0", "x86_64-pc-windows-msvc"), ("0.1.1", "aarch64-linux-android")):
            with self.subTest(version=source_version, target=target), self.assertRaises(ValueError):
                self.repackage(source_version, target)
        self.assertFalse(self.output.exists())

    def test_unmanifested_files_are_rejected(self):
        self.write_source(dict(self.payload, extra=b"not in manifest"))
        with self.assertRaisesRegex(ValueError, "exactly the archive payload"):
            self.repackage()

    def test_unsafe_payload_paths_are_rejected(self):
        for name in ("../library.lib", "/library.lib", "C:/library.lib", "lib\\library.lib"):
            self.payload = {name: b"data"}
            self.manifest["files"] = [{"path": name, "size": 4, "sha256": hashlib.sha256(b"data").hexdigest()}]
            self.write_source()
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, "Unsafe archive path"):
                self.repackage()

    def test_input_cannot_be_overwritten(self):
        with self.assertRaisesRegex(ValueError, "must differ"):
            repackager.repackage(self.source, self.source, "0.1.1", "0.1.2", "x86_64-pc-windows-msvc")


if __name__ == "__main__":
    unittest.main()
