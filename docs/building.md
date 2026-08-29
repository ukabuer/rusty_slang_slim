# Native build baselines

The production artifacts are built outside Cargo and later published through
GitHub Releases. Cargo consumers do not compile Slang.

## Pinned toolchains

| Build | Baseline |
| --- | --- |
| Windows host and target | Visual Studio 2022, MSVC x64, static MSVC runtime |
| Android target | NDK r27d (`27.3.13750724`), `arm64-v8a`, API 29, static libc++ |
| CMake | 3.25 or newer |
| Slang | Git submodule tag `v2026.16.1` |

NDK r27d is the current Android LTS toolchain. The NDK version does not set the
minimum device OS: the Android preset separately fixes `ANDROID_PLATFORM` to
`android-29`.

MSVC builds explicitly use UTF-8 source decoding, so their behavior does not
depend on the Windows system locale.

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

All generated files stay below `build` and are ignored by Git.
