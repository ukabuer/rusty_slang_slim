# Native build baselines

The production artifacts are built outside Cargo and later published through
GitHub Releases. Cargo consumers do not compile Slang.

## Pinned toolchains

| Build | Baseline |
| --- | --- |
| Windows host and target | Visual Studio 2022, MSVC x64, dynamic MSVC runtime (`/MD`) |
| Android target | NDK r27d (`27.3.13750724`), `arm64-v8a`, API 29, static libc++ |
| CMake | 3.25 or newer |
| Slang | Git submodule tag `v2026.16.1` |

NDK r27d is the current Android LTS toolchain. The NDK version does not set the
minimum device OS: the Android preset separately fixes `ANDROID_PLATFORM` to
`android-29`.

MSVC builds explicitly use UTF-8 source decoding, so their behavior does not
depend on the Windows system locale. The Windows native asset uses the dynamic
MSVC CRT (`/MD`) to match the default Rust MSVC target; consumers do not need to
enable Rust's `crt-static` target feature. The corresponding Microsoft runtime
DLLs must be available on the deployment machine.

## Bootstrap and configure

Initialize the pinned Slang tree first:

```powershell
git submodule update --init --recursive
```

Install NDK `27.3.13750724`, then run:

```powershell
./scripts/bootstrap-android.ps1 -AndroidNdkHome C:/path/to/android-ndk-r27d
```

The script performs these steps:

1. Configures the Windows x64 build.
2. Builds and installs Slang's build-host generators.
3. Configures the Android ARM64 cross build with those host tools.

It intentionally does not build the final Slang libraries. The next native
feasibility step builds `windows-x64-release` and `android-arm64-release`, then
audits the exact archive size and dependencies.

## Manual build commands

Run CMake commands from the `native` directory:

```powershell
cmake --build --preset windows-x64-release
cmake --build --preset android-arm64-release
```

To run the maintainer-only native feasibility probe after those builds:

```powershell
cmake --build --preset windows-x64-feasibility --parallel
build/native/windows-x64/Release/slang-slim-feasibility.exe `
  tests/shaders/multi-entry.hlsl build/feasibility/windows-api

cmake --build --preset windows-x64-abi --parallel
build/native/windows-x64/Release/slang-slim-abi-feasibility.exe

# Optional Release-LTO size experiment (uses a separate build directory).
cmake --build --preset windows-x64-lto-abi --parallel
build/native/windows-x64-lto/Release/slang-slim-abi-feasibility.exe

$env:ANDROID_NDK_HOME = (Resolve-Path build/toolchains/android-ndk-r27d).Path
cmake --build --preset android-arm64-feasibility --parallel
cmake --build --preset android-arm64-abi --parallel
cmake --build --preset android-arm64-lto-abi --parallel
```

The native probe and the size/dependency conclusions are recorded in
[`docs/feasibility.md`](feasibility.md).

## Package a release asset

After the non-LTO Release target has been built, package its complete static
link set from the repository root:

```powershell
./scripts/package-native.ps1 `
  -Target x86_64-pc-windows-msvc `
  -Version 0.1.0

$env:ANDROID_NDK_HOME = (Resolve-Path build/toolchains/android-ndk-r27d).Path
cmake --build --preset android-arm64-release --parallel
./scripts/package-native.ps1 `
  -Target aarch64-linux-android `
  -Version 0.1.0
```

Assets and their sibling SHA-256 files are written under `build/packages` by
default. The ZIP manifest is the contract consumed by the later
`slang-slim-sys` download/link step.

## Exercise the Rust linker locally

Before release metadata is published, point the sys crate at a locally packaged
archive. Relative paths are resolved from `crates/slang-slim-sys`:

```powershell
$env:SLANG_SLIM_NATIVE_ARCHIVE = `
  "../../build/packages/slang-slim-native-v0.0.0-x86_64-pc-windows-msvc.zip"
cargo test -p slang-slim-sys
Remove-Item Env:SLANG_SLIM_NATIVE_ARCHIVE
```

The build script verifies the sibling `.zip.sha256`, stores the archive and
validated extraction in a persistent Cargo cache, and links the libraries in
manifest order. `SLANG_SLIM_NATIVE_DIR` can instead point at an already
extracted package. `SLANG_SLIM_CACHE_DIR` overrides the cache location, and
`SLANG_SLIM_RELEASE_BASE_URL` selects a GitHub-compatible mirror.

Version `0.0.0` remains source-only when neither local override is set. A real
release adds immutable archive hashes to
`crates/slang-slim-sys/native-artifacts.json`; only then is automatic download
enabled for that crate version and target. `CARGO_NET_OFFLINE=true` and
`SLANG_SLIM_DISABLE_DOWNLOAD=1` prohibit network fallback.

The first Windows release supports the dynamic MSVC CRT only. A Cargo build
using `-C target-feature=+crt-static` is rejected until a matching static-CRT
asset is published.

All generated files stay below `build` and are ignored by Git.
