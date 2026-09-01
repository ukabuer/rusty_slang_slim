# Maintainer scripts

`bootstrap-android.ps1` configures the Windows x64 build, builds Slang's host
generators, and configures the Android ARM64/API 29 cross build.

`package-native.ps1` merges the audited static-library set into one platform
archive, writes a machine-readable link manifest, and emits a sibling SHA-256
file. Run it only after the corresponding Release preset succeeds:

The manifest records the pinned Slang commit, but intentionally does not record
the root repository `HEAD`. This keeps the archive SHA-256 stable when the
generated hash is written back to `native-artifacts.json` before tagging.

```powershell
./scripts/package-native.ps1 `
  -Target x86_64-pc-windows-msvc `
  -Version 0.1.0

./scripts/package-native.ps1 `
  -Target aarch64-linux-android `
  -Version 0.1.0
```

Keep `-Version` synchronized with the crate version. The checked-in release
index currently contains the two `0.1.0` assets.

Consumer `cargo build` must not invoke these scripts.

## CI and release gates

`.github/workflows/native.yml` is the source-of-truth maintainer workflow. It
runs Rust checks, verifies the pinned Slang gitlink, builds the Windows x64 and
Android ARM64/API 29 Release configurations, runs the Windows ABI executable
smoke test, validates the Android ARM64 ELF link output, compiles the Rust
native tests, and uploads the two deterministic archives as workflow artifacts.

The Android job installs NDK `27.3.13750724` and Ninja on a Windows runner so it
uses the same host-generator flow as the documented local build. Android is a
cross-link smoke test in CI; execution still requires a device or emulator.

The following helpers are also safe to run locally:

```powershell
./scripts/verify-slang-submodule.ps1
./scripts/native-smoke.ps1 -Target x86_64-pc-windows-msvc
./scripts/check-native-size.ps1 `
  -Target x86_64-pc-windows-msvc `
  -PackagePath build/packages/slang-slim-native-v0.1.0-x86_64-pc-windows-msvc.zip
```

`check-native-size.ps1` enforces the current budgets of 40 MiB compressed /
150 MiB uncompressed for Windows and 30 MiB compressed / 90 MiB uncompressed
for Android. A deliberate size increase should change those budgets in the
same review as the build-profile change.

Pushing a `v<crate-version>` tag runs the same jobs and then publishes both
archives and their `.sha256` sidecars through the GitHub Release job. Before a
tag is pushed, update `native-artifacts.json` with the generated archive names
and hashes. The update can be generated from the two package outputs with:

```powershell
./scripts/update-native-artifacts.ps1 `
  -Version 0.1.1 `
  -PackageDirectory build/packages
```

Commit the resulting JSON change together with the version bump before pushing
the tag. `verify-native-release.ps1` makes the release fail if the checked-in
index does not exactly match the generated assets, preventing consumers from
downloading an unindexed or incorrectly hashed archive.
