"""Check a native ZIP's manifest and compressed and uncompressed size budgets."""

import argparse
import hashlib
import os
from pathlib import Path
import zipfile

from native_archive import read_native_archive


MIB = 1024 * 1024
SIZE_BUDGETS = {
    "x86_64-pc-windows-msvc": (40, 150),
    "aarch64-linux-android": (30, 90),
}


def check_size(package_path, target, max_archive_mib=0, max_payload_mib=0):
    default_archive, default_payload = SIZE_BUDGETS[target]
    archive_limit = max_archive_mib if max_archive_mib > 0 else default_archive
    payload_limit = max_payload_mib if max_payload_mib > 0 else default_payload
    if not package_path.is_file():
        raise ValueError(f"Native package does not exist: {package_path}")

    digest = hashlib.sha256()
    with package_path.open("rb") as stream:
        archive_size = os.fstat(stream.fileno()).st_size
        if archive_size > archive_limit * MIB:
            raise ValueError(
                f"Native package {package_path.name} is {archive_size} bytes, "
                f"above the {archive_limit} MiB archive budget"
            )
        with zipfile.ZipFile(stream) as archive:
            manifest, _ = read_native_archive(archive, target)
        payload_size = sum(item["size"] for item in manifest["files"])
        if payload_size > payload_limit * MIB:
            raise ValueError(
                f"Native package payload is {payload_size} bytes, "
                f"above the {payload_limit} MiB payload budget"
            )

        stream.seek(0)
        while chunk := stream.read(MIB):
            digest.update(chunk)

    return (
        f"Native package {package_path.name}: archive={archive_size / MIB:.2f} MiB/{archive_limit} MiB, "
        f"payload={payload_size / MIB:.2f} MiB/{payload_limit} MiB, SHA-256={digest.hexdigest()}"
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", required=True, choices=SIZE_BUDGETS)
    parser.add_argument("--package-path", required=True, type=Path)
    parser.add_argument("--max-archive-mib", type=int, default=0)
    parser.add_argument("--max-payload-mib", type=int, default=0)
    args = parser.parse_args()
    package_path = Path(__file__).resolve().parents[1] / args.package_path
    try:
        print(check_size(package_path, args.target, args.max_archive_mib, args.max_payload_mib))
    except (OSError, ValueError, zipfile.BadZipFile) as error:
        parser.exit(1, f"error: {error}\n")


if __name__ == "__main__":
    main()
