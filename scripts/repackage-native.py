"""Give verified native binaries a new package version without compiling them."""

import argparse
import copy
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import tempfile
import zipfile


def repackage(source, destination, source_version, version, target):
    source, destination = Path(source), Path(destination)
    if source.resolve() == destination.resolve():
        raise ValueError("Source and destination archives must differ")
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?", version):
        raise ValueError("Invalid native package version")
    temporary = None
    try:
        with zipfile.ZipFile(source) as archive:
            entries = {entry.filename: entry for entry in archive.infolist() if not entry.is_dir()}
            if len(entries) != len([entry for entry in archive.infolist() if not entry.is_dir()]):
                raise ValueError("Duplicate archive entries")
            manifest = json.loads(archive.read("manifest.json"))
            if (manifest.get("schema_version") != 1 or manifest.get("abi_version") != 1
                    or manifest.get("package") != "slang-slim-native"
                    or manifest.get("version") != source_version
                    or manifest.get("target") != target
                    or manifest.get("link", {}).get("kind") != "static"):
                raise ValueError("Source native manifest does not match the requested package")
            files = {item["path"]: item for item in manifest["files"]}
            for name in set(entries) | set(files):
                path = PurePosixPath(name)
                if path.is_absolute() or ".." in path.parts or "\\" in name or ":" in name:
                    raise ValueError("Unsafe archive path")
            if len(files) != len(manifest["files"]) or set(entries) != set(files) | {"manifest.json"}:
                raise ValueError("Native manifest does not describe exactly the archive payload")
            destination.parent.mkdir(parents=True, exist_ok=True)
            with tempfile.NamedTemporaryFile(dir=destination.parent, suffix=".zip.tmp", delete=False) as handle:
                temporary = Path(handle.name)
            manifest["binary_source_version"] = manifest.get("binary_source_version", source_version)
            manifest["version"] = version
            with zipfile.ZipFile(temporary, "w", compression=zipfile.ZIP_DEFLATED) as output:
                for name, item in files.items():
                    digest, size = hashlib.sha256(), 0
                    entry = copy.copy(entries[name])
                    entry.compress_type = zipfile.ZIP_DEFLATED
                    with archive.open(name) as incoming, output.open(entry, "w") as outgoing:
                        while data := incoming.read(1024 * 1024):
                            digest.update(data)
                            size += len(data)
                            outgoing.write(data)
                    if size != item["size"] or digest.hexdigest() != item["sha256"]:
                        raise ValueError(f"Native payload checksum or size mismatch: {name}")
                output.writestr(copy.copy(entries["manifest.json"]),
                                json.dumps(manifest, indent=2) + "\n")
        os.replace(temporary, destination)
        temporary = None
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--source-version", required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--target", required=True,
                        choices=("x86_64-pc-windows-msvc", "aarch64-linux-android"))
    args = parser.parse_args()
    repackage(args.source, args.output, args.source_version, args.version, args.target)
    print(f"Repackaged {args.source_version} binaries as {args.version}: {args.output}")


if __name__ == "__main__":
    main()
