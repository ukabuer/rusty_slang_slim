# Native build baselines

The production artifacts are built outside Cargo and later published through
GitHub Releases. Cargo consumers do not compile Slang.

## Pinned toolchains

| Build | Baseline |
| --- | --- |
| Windows host and target | Visual Studio 2022, MSVC x64, dynamic MSVC runtime (`/MD`) |
| Android target | NDK r27d (`27.3.13750724`), `arm64-v8a`, API 29, static libc++ and libc++abi |
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
  -Version 0.1.1

$env:ANDROID_NDK_HOME = (Resolve-Path build/toolchains/android-ndk-r27d).Path
cmake --build --preset android-arm64-release --parallel
./scripts/package-native.ps1 `
  -Target aarch64-linux-android `
  -Version 0.1.1
```

Assets are written under `build/packages` by default. The ZIP manifest is the
contract consumed by the `slang-slim-sys` download/link step. Keep `-Version`
synchronized with the crate version. GitHub computes and exposes each Release
asset's SHA-256 `digest`; the Rust build script uses that API metadata for
remote archive integrity verification.

## Exercise the Rust linker locally

For local validation, point the sys crate at a locally packaged archive.
Relative paths are resolved from `crates/slang-slim-sys`:

```powershell
$env:SLANG_SLIM_NATIVE_ARCHIVE = `
  "../../build/packages/slang-slim-native-v0.1.1-x86_64-pc-windows-msvc.zip"
cargo test -p slang-slim-sys
Remove-Item Env:SLANG_SLIM_NATIVE_ARCHIVE
```

The build script stores the archive and validated extraction in a persistent Cargo
cache, then links the single merged library listed in the manifest.
`SLANG_SLIM_NATIVE_DIR` can instead point at an already extracted package.
`SLANG_SLIM_CACHE_DIR` overrides the cache location, and
`SLANG_SLIM_RELEASE_BASE_URL` selects a GitHub-compatible mirror.

For a published build, the archive URL is derived from the crate version and
Rust target as
`<base>/v<version>/slang-slim-native-v<version>-<target>.zip`. With the
default GitHub Release base URL, the build script queries the Release API for
the matching asset's SHA-256 `digest`, caches that metadata, and verifies
the downloaded archive. A custom mirror must be paired with
`SLANG_SLIM_NATIVE_SHA256` because the project cannot infer its API digest.

For local validation, build and package the native target first, then point Cargo
at the resulting ZIP:

```powershell
./scripts/package-native.ps1 -Target x86_64-pc-windows-msvc -Version 0.1.1
$env:SLANG_SLIM_NATIVE_ARCHIVE = `
  "../../build/packages/slang-slim-native-v0.1.1-x86_64-pc-windows-msvc.zip"
cargo test --workspace -- --nocapture
cargo build -p slang-slim --example multi_target_compile
& .\target\debug\examples\multi_target_compile.exe
Remove-Item Env:SLANG_SLIM_NATIVE_ARCHIVE
```

For Android, build and package `android-arm64-release`, set
`ANDROID_NDK_HOME` (or `ANDROID_NDK_ROOT`), and pass
`--target aarch64-linux-android` to Cargo. An already extracted package can
be selected with `SLANG_SLIM_NATIVE_DIR`. Android Rust links also need the NDK
`libc++_static.a` and `libc++abi.a`.

For a one-command maintainer build from the checked-out Slang source, set
`SLANG_SLIM_FROM_SOURCE=1`. Cargo configures the matching CMake preset when
needed, builds the Release target, and links the local CMake libraries directly.
This path skips the archive download and GitHub Release API query. It takes
precedence over `SLANG_SLIM_NATIVE_ARCHIVE` and `SLANG_SLIM_NATIVE_DIR`.
For Android, the source path also builds and installs the Windows host generators;
set `ANDROID_NDK_HOME` or `ANDROID_NDK_ROOT` first. The bundled
`build/toolchains/android-ndk-r27d` path is also accepted, and the Android
source path currently assumes a Windows host, matching CI.

```powershell
$env:SLANG_SLIM_FROM_SOURCE = "1"
cargo test --workspace -- --nocapture
cargo build -p slang-slim --example multi_target_compile
& .\target\debug\examples\multi_target_compile.exe
Remove-Item Env:SLANG_SLIM_FROM_SOURCE
```

For an Android source build, install the Rust target and run the same switch
with the cross target (this links the library but does not try to run an Android
binary on the host):

```powershell
rustup target add aarch64-linux-android
$env:SLANG_SLIM_FROM_SOURCE = "1"
cargo build -p slang-slim --target aarch64-linux-android
Remove-Item Env:SLANG_SLIM_FROM_SOURCE
```

When the source switch is not set, Cargo does not invoke CMake or build Slang;
it only uses a local archive/directory override or the published native asset.

Any published version with both target archives enables automatic download for the
Windows and Android targets because the `native` feature is enabled by default.
Use `--no-default-features` for source-only workspace checks that should not
prepare a native asset. `CARGO_NET_OFFLINE=true` and
`SLANG_SLIM_DISABLE_DOWNLOAD=1` prohibit network fallback; provide a local
archive, `SLANG_SLIM_NATIVE_SHA256`, or a populated cache in those modes.

The first Windows release supports the dynamic MSVC CRT only. A Cargo build
using `-C target-feature=+crt-static` is rejected until a matching static-CRT
asset is published.

## Continuous integration and releases

The repository workflow at [`.github/workflows/native.yml`](../.github/workflows/native.yml)
uses Cargo metadata to check that both crate versions and the sys dependency
requirement agree. It also checks formatting, release planner and repackaging
tests, and the workspace with `--locked` to catch a stale `Cargo.lock`.
The native planner finds a published release with both Windows and Android archives
whose tag is an ancestor of the current commit, then compares native inputs:
sources, CMake configuration, the Slang gitlink, native packaging/bootstrap
scripts, and the native workflow recipe (including its toolchain versions).

| Native state | Action |
| --- | --- |
| Inputs changed, or no complete baseline can be verified | Compile both native targets. |
| Inputs unchanged, release version is new | Repackage the previous binaries with the new package version. |
| Inputs unchanged, both archives already exist for this version | Reuse those archives. |

The planner checks the current version first, then searches published ancestor
releases for a complete baseline. API errors fail the check rather than being
interpreted as missing assets. Repackaging verifies all payload checksums and
preserves library/header bytes, link metadata, and the pinned Slang commit. Only
package metadata changes; `binary_source_version` records the original binary
release even after successive repackaging.

When compilation is required, the workflow verifies the Slang submodule,
builds both Release configurations, runs the Windows ABI smoke executable, and
validates the Android ARM64 ELF link result. An exact cache of the packaged
native outputs lets a tag reuse a successful `main` build with the same native
inputs, release version, target, and planning/repackaging scripts. No partial
cache matches are used. The manual `force_build` input bypasses both archive
reuse and this cache.

All paths check archive size budgets, run the Windows Rust integration tests
and example, and cross-link the Android Rust tests and example with NDK
`27.3.13750724`. Reusing native libraries does not skip testing Rust wrapper
changes. Main and tag runs upload the validated ZIPs as artifacts retained for
30 days. Tags run their own validation through the reusable native workflow;
they do not wait for a separate main workflow run.

### Crate release tags

[`.github/workflows/release.yml`](../.github/workflows/release.yml) uses one tag
rule, `vX.Y.Z`, for every release. Both crates and the native package use the
same version. A change confined to the safe wrapper still increments both crate
versions; unchanged native inputs reuse the compiled binaries through
repackaging. This preserves the existing consumer download URL and manifest
version checks without requiring a separate native tag or version index.

Both crates inherit `[workspace.package].version` from the root `Cargo.toml`.

1. Set `[workspace.package].version` and the `slang-slim-sys` version in
   `[workspace.dependencies]` to the new version in the root `Cargo.toml`.
   Run `cargo update --workspace` to refresh `Cargo.lock`.
2. Commit the release change, push `main`, and let CI validate it.
3. Push the matching `v<version>` tag, such as `v0.1.2`.

The release check validates the tag against the versions resolved by Cargo
before native compilation and publication.

The workflow stages both native assets on the GitHub Release, makes the
release public, and then publishes `slang-slim-sys` followed by `slang-slim`.
Each crate first passes `cargo publish --dry-run`. Packaging checks use
`--no-default-features` on Linux. The native CI checks both targets with the
default `native` feature before publication.
Each publish job checks crates.io again on retries and skips an already
published version. The native release job uploads missing archives and can
resume a partially uploaded draft.

### One-time crates.io authentication setup

If a crate is new to crates.io, publish it once with a crates.io API token
after the matching native assets are public. Publish `slang-slim-sys` before
`slang-slim`. Set up Trusted Publishing for subsequent versions.

For each crate, add a GitHub Actions Trusted Publisher in its crates.io settings.

- Repository owner: `ukabuer`
- Repository: `rusty_slang_slim`
- Workflow filename: `release.yml`
- Environment: leave empty (the workflow does not specify one)

The publish jobs grant `id-token: write` and use
[`rust-lang/crates-io-auth-action`](https://github.com/rust-lang/crates-io-auth-action)
to obtain a temporary publishing token. Configure both Trusted Publishers
before the first automated Cargo release.

All generated files stay below `build` and are ignored by Git.
