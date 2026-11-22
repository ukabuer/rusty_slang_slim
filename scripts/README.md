# Maintainer scripts

`bootstrap-android.ps1` configures the Windows x64 build, builds Slang's host
generators, and configures the Android ARM64/API 29 cross build.

`package-native.ps1` merges the audited static-library set into one platform
archive and writes a machine-readable link manifest. Run it only after the
corresponding Release preset succeeds:

The manifest records the pinned Slang commit, but intentionally does not record
the root repository `HEAD`. This keeps the archive bytes stable when the
release workflow publishes the asset; GitHub records the ZIP's SHA-256 digest.

```powershell
./scripts/package-native.ps1 `
  -Target x86_64-pc-windows-msvc `
  -Version 0.1.1

./scripts/package-native.ps1 `
  -Target aarch64-linux-android `
  -Version 0.1.1
```

Keep `-Version` synchronized with the crate version. The output ZIP uses the
same version/target name that the consumer build derives automatically.

Consumer `cargo build` must not invoke these scripts.

Maintainers can set `SLANG_SLIM_FROM_SOURCE=1` for a one-command local source
build. Cargo configures/builds the matching CMake Release target and links it
directly; archive downloads and Release API queries are skipped. Android source
builds accept the bundled `build/toolchains/android-ndk-r27d` NDK or an NDK
selected with `ANDROID_NDK_HOME`/`ANDROID_NDK_ROOT`; the Android path currently
assumes a Windows host, matching CI.

The consumer build derives release URLs without a hand-maintained path map:
`<base>/v<version>/slang-slim-native-v<version>-<target>.zip`. With the default
GitHub base URL it queries the Release API for the asset's SHA-256 `digest`,
caches that metadata, and verifies the archive before extraction.

## CI and release gates

`plan-release.py` compares native inputs with a complete published ancestor
release. It chooses compilation when inputs changed or no baseline is verified,
repackaging when unchanged binaries need a new release version, and direct
reuse when this version already has both target archives. It also validates
the workspace versions and sys dependency requirement using Cargo metadata, and
checks the unified release tag when supplied. The `--check-crate` option reads
the exact version's publication status for a Cargo publish job. Run the
regression tests with Cargo on `PATH` and
`python -m unittest discover -s scripts/tests -v`.

`repackage-native.py` verifies the previous package's payload sizes/checksums,
preserves library/header bytes and link metadata, updates its package version,
and records the original `binary_source_version`. It never invokes a compiler.

`.github/workflows/native.yml` uses that plan to build, repackage, or reuse
published archives. An exact archive cache avoids recompiling the same native
inputs for a main build and its tag. All paths run Rust integration/link checks
and archive checks. Manual `force_build` bypasses reuse and caching.

The Android job installs NDK `27.3.13750724` and Ninja on a Windows runner so it
uses the same host-generator flow as the documented local build. Android is a
cross-link smoke test in CI; execution still requires a device or emulator.

The following helpers are also safe to run locally:

```powershell
./scripts/verify-slang-submodule.ps1
./scripts/native-smoke.ps1 -Target x86_64-pc-windows-msvc
./scripts/check-native-size.ps1 `
  -Target x86_64-pc-windows-msvc `
  -PackagePath build/packages/slang-slim-native-v0.1.1-x86_64-pc-windows-msvc.zip
```

`check-native-size.ps1` enforces the current budgets of 40 MiB compressed /
150 MiB uncompressed for Windows and 30 MiB compressed / 90 MiB uncompressed
for Android. A deliberate size increase should change those budgets in the
same review as the build-profile change.

Every release uses `v<version>`. Update the inherited package version and the
sys dependency requirement in the root `Cargo.toml` and refresh `Cargo.lock`.
The native package uses the same version when repackaging existing binaries.
Each tag validates both targets through the reusable native workflow, then
publishes native assets, `slang-slim-sys`, and `slang-slim` in order. The release
workflow uses crates.io Trusted Publishing and skips published versions.
Workflow artifacts are retained for 30 days. See
[`docs/building.md`](../docs/building.md#continuous-integration-and-releases)
for version rules and the required one-time Trusted Publisher setup.
