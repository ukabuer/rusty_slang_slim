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
default. The ZIP manifest is the contract consumed by the
`slang-slim-sys` download/link step. Keep `-Version` synchronized with the
crate version. The `.zip.sha256` sidecar is consumed by `slang-slim-sys` for
remote archive integrity verification.

## Exercise the Rust linker locally

For local validation, point the sys crate at a locally packaged archive.
Relative paths are resolved from `crates/slang-slim-sys`:

```powershell
$env:SLANG_SLIM_NATIVE_ARCHIVE = `
  "../../build/packages/slang-slim-native-v0.1.0-x86_64-pc-windows-msvc.zip"
cargo test -p slang-slim-sys
Remove-Item Env:SLANG_SLIM_NATIVE_ARCHIVE
```

The build script verifies the sibling `.zip.sha256`, stores the archive and
validated extraction in a persistent Cargo cache, and links the single merged
library listed in the manifest. `SLANG_SLIM_NATIVE_DIR` can instead point at an already
extracted package. `SLANG_SLIM_CACHE_DIR` overrides the cache location, and
`SLANG_SLIM_RELEASE_BASE_URL` selects a GitHub-compatible mirror.

For a published build, the archive URL is derived from the crate version and
Rust target as
`<base>/v<version>/slang-slim-native-v<version>-<target>.zip`, and the checksum
is fetched from the same URL with `.sha256` appended. The checksum sidecar is
cached alongside the downloaded archive metadata.

If CMake has already built the native libraries, Cargo can link that tree
directly without creating a ZIP or downloading anything. This is the explicit
directory override (the one-command `SLANG_SLIM_FROM_SOURCE=1` path is shown
below):

```powershell
cmake --build --preset windows-x64-release --parallel
$env:SLANG_SLIM_NATIVE_BUILD_DIR = (Resolve-Path build/native/windows-x64).Path
cargo test --workspace --features native-tests -- --nocapture
cargo build -p slang-slim --features native-tests --example multi_target_compile
& .\target\debug\examples\multi_target_compile.exe
Remove-Item Env:SLANG_SLIM_NATIVE_BUILD_DIR
```

For Android, build `android-arm64-release`, set the variable to
`build/native/android-arm64`, and pass `--target aarch64-linux-android` to
Cargo. This direct-build override uses the known CMake Release library layout,
is supported for development only, and is intentionally not checksum-validated
or used for published consumer builds. `SLANG_SLIM_NATIVE_BUILD_DIR` is
mutually exclusive with `SLANG_SLIM_NATIVE_DIR` and
`SLANG_SLIM_NATIVE_ARCHIVE`. If `SLANG_SLIM_FROM_SOURCE=1` is set, it takes
precedence over all native archive/directory overrides.

The shorter source-build path is preferred when changing Slang or the native
bridge. Set `SLANG_SLIM_FROM_SOURCE=1` and Cargo will configure the matching
CMake preset when needed, build the Release native target, and link it into the
Rust tests or example automatically:

```powershell
$env:SLANG_SLIM_FROM_SOURCE = "1"
cargo test --workspace --features native-tests -- --nocapture
cargo build -p slang-slim --features native-tests --example multi_target_compile
& .\target\debug\examples\multi_target_compile.exe
Remove-Item Env:SLANG_SLIM_FROM_SOURCE
```

For an Android cross build, set `ANDROID_NDK_HOME` (or keep the pinned NDK at
`build/toolchains/android-ndk-r27d`) and add
`--target aarch64-linux-android` to the Cargo command. The source mode also
builds and installs the Windows host generators required by the Android Slang
configuration. It is analogous to `rusty_v8`'s `V8_FROM_SOURCE=1` mode and is
intended for maintainer/development builds, not published consumer builds.

Any published version with both target archives and their `.sha256` sidecars
enables automatic download for the Windows and Android targets when the
`native` feature is enabled. Builds without that feature remain source-only,
so ordinary workspace checks do not need a native asset.
`CARGO_NET_OFFLINE=true` and `SLANG_SLIM_DISABLE_DOWNLOAD=1` prohibit network
fallback; provide a local archive, `SLANG_SLIM_NATIVE_SHA256`, or a populated
cache in those modes.

The first Windows release supports the dynamic MSVC CRT only. A Cargo build
using `-C target-feature=+crt-static` is rejected until a matching static-CRT
asset is published.

## Continuous integration and releases

The repository workflow at [`.github/workflows/native.yml`](../.github/workflows/native.yml)
is the reproducible maintainer path. It checks formatting and the workspace,
verifies the Slang submodule revision, installs the pinned Android NDK
`27.3.13750724`, builds both Release native configurations, runs the Windows
ABI smoke executable, validates the Android ARM64 ELF link result, compiles the
Rust native tests, and checks archive size budgets. The release archives are
uploaded only on `v<crate-version>` tag runs.

Tags in the form `v<crate-version>` additionally publish the two archives and
their SHA-256 sidecars as a GitHub Release. No repository-side artifact index
needs to be edited: the tag CI job builds the archives, generates the sidecars,
and publishes both files under the derived version/target names.

All generated files stay below `build` and are ignored by Git.
