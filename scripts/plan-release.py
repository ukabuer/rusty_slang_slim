"""Plan native reuse and crate publication without changing remote state."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import urllib.error
import urllib.request


CRATES = ("slang-slim-sys", "slang-slim")
TARGETS = ("x86_64-pc-windows-msvc", "aarch64-linux-android")
NATIVE_INPUTS = (
    ".gitmodules",
    "third_party/slang",
    "native/CMakeLists.txt",
    "native/CMakePresets.json",
    "native/ci-toolchains.json",
    "native/cmake",
    "native/include",
    "native/src",
    # The ABI probe must be rebuilt and checked when its source or smoke check changes.
    "native/feasibility/abi_main.cpp",
    "scripts/build-native.ps1",
    "scripts/native-smoke.ps1",
    "scripts/package-native.ps1",
    "scripts/bootstrap-android.ps1",
    "scripts/verify-slang-submodule.ps1",
)
NON_DOCUMENTATION_PATHS = (".", ":(exclude,glob)docs/**", ":(exclude,glob)**/README.md")
VERSION_PATTERN = r"[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?"


def workspace_version(root, tag):
    metadata = subprocess.run(
        ["cargo", "metadata", "--no-deps", "--format-version", "1", "--offline"],
        cwd=root, stdout=subprocess.PIPE, encoding="utf-8", check=True,
    )
    packages = {package["name"]: package for package in json.loads(metadata.stdout)["packages"]}
    sys_version = packages["slang-slim-sys"]["version"]
    safe = packages["slang-slim"]
    if safe["version"] != sys_version:
        raise ValueError(f"Both crate versions must match: slang-slim is {safe['version']}, "
                         f"slang-slim-sys is {sys_version}")
    if tag is not None and tag != f"v{sys_version}":
        raise ValueError(f"Tag {tag} does not match the crate versions; expected v{sys_version}")

    sys_dependency = next(
        (dependency for dependency in safe["dependencies"]
         if dependency["name"] == "slang-slim-sys" and dependency["kind"] is None),
        None,
    )
    requirement = sys_dependency["req"] if sys_dependency else "missing"
    # Cargo normalizes a bare version to a caret requirement and ignores build metadata.
    if requirement != "^" + sys_version.split("+", 1)[0]:
        raise ValueError(f"slang-slim must declare slang-slim-sys version {sys_version}; found {requirement}")
    return sys_version


def get_json(url):
    headers = {"User-Agent": "slang-slim-release-planner", "Accept": "application/json"}
    token = os.environ.get("GH_TOKEN")
    if url.startswith("https://api.github.com/") and token:
        headers["Authorization"] = f"Bearer {token}"
    request = urllib.request.Request(url, headers=headers)
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            return json.load(response)
    except urllib.error.HTTPError as error:
        if error.code == 404:
            return None
        raise RuntimeError(f"Release metadata request failed with HTTP {error.code}: {url}") from error


def crate_needs_publish(crate, version, read_json=get_json):
    return read_json(f"https://crates.io/api/v1/crates/{crate}/{version}") is None


def changed_since(root, reference, paths):
    resolved = subprocess.run(
        ["git", "rev-parse", "--verify", "--end-of-options", f"{reference}^{{commit}}"],
        cwd=root, capture_output=True, text=True,
    )
    if resolved.returncode:
        return None
    result = subprocess.run(
        ["git", "diff", "--quiet", "--no-renames", "--ignore-submodules=none",
         resolved.stdout.strip(), "HEAD", "--", *paths], cwd=root,
    )
    if result.returncode not in (0, 1):
        raise RuntimeError(f"Cannot compare native inputs against {reference}")
    return result.returncode == 1


def complete_native_release(version, release):
    expected = {f"slang-slim-native-v{version}-{target}.zip" for target in TARGETS}
    assets = {asset["name"] for asset in release.get("assets", [])} if release else set()
    return bool(release) and not release.get("draft", False) and expected.issubset(assets)


def is_ancestor(root, reference):
    result = subprocess.run(
        ["git", "merge-base", "--is-ancestor", reference, "HEAD"],
        cwd=root, capture_output=True,
    )
    if result.returncode not in (0, 1, 128):
        raise RuntimeError(f"Cannot resolve native baseline {reference}")
    return result.returncode == 0


def previous_native_version(root, repository, version, read_json, ancestor):
    page = 1
    while True:
        releases = read_json(
            f"https://api.github.com/repos/{repository}/releases?per_page=100&page={page}"
        )
        if not isinstance(releases, list):
            raise RuntimeError("Cannot list published native releases")
        for release in releases:
            tag = release.get("tag_name", "")
            if not re.fullmatch("v" + VERSION_PATTERN, tag) or tag == f"v{version}":
                continue
            candidate = tag[1:]
            if complete_native_release(candidate, release) and ancestor(root, tag):
                return candidate
        if len(releases) < 100:
            return None
        page += 1


def native_decision(version, release, source_version, changed, force=False):
    complete = complete_native_release(version, release)
    if force:
        reason = "Native compilation explicitly requested"
    elif not source_version or changed is None:
        reason = "No published native baseline can be verified"
    elif changed:
        reason = f"Native inputs differ from v{source_version}"
    elif complete:
        reason = f"Native inputs match v{version}; reuse both published archives"
    else:
        reason = f"Native inputs match v{source_version}; repackage its binaries as v{version}"
    build = force or not source_version or changed is not False
    action = "build" if build else "reuse" if complete else "repackage"
    return {
        "native_version": version,
        "source_version": source_version or "",
        "native_action": action,
        "build_native": bool(build),
        "publish_native": not complete,
        "force_build": force,
        "reason": reason,
    }


def native_source_key(root):
    tree = subprocess.run(
        ["git", "ls-tree", "-r", "HEAD", "--", *NATIVE_INPUTS],
        cwd=root, capture_output=True, check=True,
    ).stdout
    return hashlib.sha256(tree).hexdigest()


def plan(root, repository, tag=None, force=False, read_json=get_json,
         compare=changed_since, ancestor=is_ancestor, base=None):
    version = workspace_version(root, tag)
    if base and not tag and not force and compare(root, base, NON_DOCUMENTATION_PATHS) is False:
        return {"run_native": False, "reason": "Only documentation changed"}

    release = read_json(f"https://api.github.com/repos/{repository}/releases/tags/v{version}")
    source_version = version if complete_native_release(version, release) else previous_native_version(
        root, repository, version, read_json, ancestor
    )
    changed = compare(root, f"v{source_version}", NATIVE_INPUTS) if source_version else None
    result = native_decision(version, release, source_version, changed, force)
    result["run_native"] = True
    result["native_source_key"] = native_source_key(root)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository", default=os.environ.get("GITHUB_REPOSITORY"))
    parser.add_argument("--tag")
    parser.add_argument("--base", help="Skip native validation for documentation-only changes since this commit")
    parser.add_argument("--check-crate", choices=CRATES, help="Check crates.io for this crate at --tag")
    parser.add_argument("--force-build", action="store_true")
    parser.add_argument("--github-output", default=os.environ.get("GITHUB_OUTPUT"))
    args = parser.parse_args()
    if args.check_crate:
        if not args.tag or not re.fullmatch("v" + VERSION_PATTERN, args.tag):
            parser.error("--check-crate requires --tag v<version>")
        result = {"publish": crate_needs_publish(args.check_crate, args.tag[1:])}
    else:
        if not args.repository or not re.fullmatch(r"[\w.-]+/[\w.-]+", args.repository):
            parser.error("--repository must be an owner/repository pair")
        root = Path(__file__).resolve().parent.parent
        try:
            result = plan(root, args.repository, args.tag, args.force_build, base=args.base)
        except ValueError as error:
            parser.exit(1, f"error: {error}\n")
    print(json.dumps(result, indent=2))
    if args.github_output:
        with open(args.github_output, "a", encoding="utf-8") as output:
            for key, value in result.items():
                text = str(value).lower() if isinstance(value, bool) else str(value)
                if "\n" in text or "\r" in text:
                    raise ValueError(f"Invalid multiline output {key}")
                output.write(f"{key}={text}\n")


if __name__ == "__main__":
    main()
