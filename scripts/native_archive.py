"""Read the manifest and validate the file layout of a native archive."""

import json
from pathlib import PurePosixPath


MANIFEST_NAME = "manifest.json"
LIBRARY_PATHS = {
    "x86_64-pc-windows-msvc": "lib/slang-slim.lib",
    "aarch64-linux-android": "lib/libslang-slim.a",
}


def read_native_archive(archive, target, version=None):
    entries = {}
    for entry in archive.infolist():
        if entry.is_dir():
            continue
        if entry.filename in entries:
            raise ValueError(f"Duplicate archive entry: {entry.filename}")
        entries[entry.filename] = entry

    if MANIFEST_NAME not in entries:
        raise ValueError(f"Native package has no {MANIFEST_NAME}")
    manifest = json.loads(archive.read(entries[MANIFEST_NAME]))
    if (not isinstance(manifest, dict) or manifest.get("schema_version") != 1
            or manifest.get("abi_version") != 1 or manifest.get("package") != "slang-slim-native"
            or manifest.get("target") != target
            or (version is not None and manifest.get("version") != version)):
        raise ValueError("Native manifest does not match the requested package")

    items = manifest.get("files")
    if not isinstance(items, list) or not all(
        isinstance(item, dict) and isinstance(item.get("path"), str) for item in items
    ):
        raise ValueError("Native manifest must list payload files")
    files = {item["path"]: item for item in items}
    for name in files:
        path = PurePosixPath(name)
        if not name or path.is_absolute() or ".." in path.parts or "\\" in name or ":" in name:
            raise ValueError(f"Unsafe archive path: {name}")
    if len(files) != len(items) or files.keys() != entries.keys() - {MANIFEST_NAME}:
        raise ValueError("Native manifest does not describe exactly the archive payload")
    for name, item in files.items():
        size = item.get("size")
        if type(size) is not int or size != entries[name].file_size:
            raise ValueError(f"Native payload size mismatch: {name}")

    link = manifest.get("link")
    if not isinstance(link, dict) or link.get("kind") != "static":
        raise ValueError("Native package must use static linking")
    library_path = LIBRARY_PATHS[target]
    libraries = link.get("libraries")
    if (not isinstance(libraries, list) or len(libraries) != 1
            or not isinstance(libraries[0], dict) or libraries[0].get("name") != "slang-slim"
            or libraries[0].get("path") != library_path):
        raise ValueError(f"Native manifest must link one slang-slim library at {library_path}")
    if {name for name in files if name.startswith("lib/")} != {library_path}:
        raise ValueError(f"Native package must contain exactly one static library at {library_path}")

    return manifest, entries
