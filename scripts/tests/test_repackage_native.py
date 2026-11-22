import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile


SCRIPTS = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "repackage_native", SCRIPTS / "repackage-native.py"
)
repackager = importlib.util.module_from_spec(SPEC)
with patch.object(sys, "path", [str(SCRIPTS), *sys.path]):
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

    def write_source(self, payload=None, compression=zipfile.ZIP_DEFLATED):
        with zipfile.ZipFile(self.source, "w", compression=compression) as archive:
            for name, data in (payload or self.payload).items():
                archive.writestr(name, data)
            archive.writestr("manifest.json", json.dumps(self.manifest))

    def repackage(self, source_version="0.1.1", target="x86_64-pc-windows-msvc"):
        repackager.repackage(self.source, self.output, source_version, "0.1.2", target)

    def check_size(self, *args):
        return subprocess.run(
            [sys.executable, "-B", str(SCRIPTS / "check-native-size.py"),
             "--target", self.manifest["target"], "--package-path", str(self.source), *args],
            cwd=self.root, capture_output=True, text=True,
        )

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
        library_path = "lib/slang-slim.lib"
        payload = dict(self.payload)
        payload[library_path] = b"x" * len(payload[library_path])
        self.write_source(payload)
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

    def test_size_check_reports_the_default_budgets_and_archive_checksum(self):
        result = self.check_size("--max-archive-mib", "0", "--max-payload-mib", "-1")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("MiB/40 MiB", result.stdout)
        self.assertIn("MiB/150 MiB", result.stdout)
        self.assertIn(hashlib.sha256(self.source.read_bytes()).hexdigest(), result.stdout)

    def test_size_check_enforces_both_budget_overrides(self):
        data = b"x" * (1024 * 1024 + 1)
        self.payload["lib/slang-slim.lib"] = data
        self.manifest["files"][0].update(size=len(data), sha256=hashlib.sha256(data).hexdigest())
        self.write_source(compression=zipfile.ZIP_STORED)
        for kind in ("archive", "payload"):
            with self.subTest(budget=kind):
                result = self.check_size(f"--max-{kind}-mib", "1")
                self.assertEqual(result.returncode, 1)
                self.assertIn(f"above the 1 MiB {kind} budget", result.stderr)

    def test_both_archive_tools_reject_an_extra_static_library(self):
        extra_path = "lib/extra.lib"
        extra_data = b"unexpected extra static library"
        self.payload[extra_path] = extra_data
        self.manifest["files"].append({
            "path": extra_path, "size": len(extra_data), "sha256": hashlib.sha256(extra_data).hexdigest(),
        })
        self.write_source()
        with self.assertRaisesRegex(ValueError, "exactly one static library"):
            self.repackage()
        result = self.check_size()
        self.assertEqual(result.returncode, 1)
        self.assertIn("exactly one static library", result.stderr)


if __name__ == "__main__":
    unittest.main()
