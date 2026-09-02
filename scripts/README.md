# Maintainer scripts

`bootstrap-android.ps1` configures the Windows x64 build, builds Slang's host
generators, and configures the Android ARM64/API 29 cross build.

`package-native.ps1` merges the audited static-library set into one platform
archive, writes a machine-readable link manifest, and emits a sibling SHA-256
file. Run it only after the corresponding Release preset succeeds:

The manifest records the pinned Slang commit, but intentionally does not record
the root repository `HEAD`. This keeps the archive SHA-256 stable when the
tag CI job publishes the archive and its checksum sidecar.

```powershell
./scripts/package-native.ps1 `
  -Target x86_64-pc-windows-msvc `
  -Version 0.1.0

./scripts/package-native.ps1 `
  -Target aarch64-linux-android `
  -Version 0.1.0
```

Keep `-Version` synchronized with the crate version. The output ZIP and its
`.zip.sha256` sidecar use the same version/target names that the consumer
build derives automatically.

Consumer `cargo build` must not invoke these scripts.

The consumer build derives release URLs without a hand-maintained path map:
`<base>/v<version>/slang-slim-native-v<version>-<target>.zip`. It downloads the
matching `.zip.sha256` sidecar and verifies the archive before extraction.

## CI and release gates

`.github/workflows/native.yml` is the source-of-truth maintainer workflow. It
runs Rust checks, verifies the pinned Slang gitlink, builds the Windows x64 and
Android ARM64/API 29 Release configurations, runs the Windows ABI executable
smoke test, validates the Android ARM64 ELF link output, compiles the Rust
native tests, and checks the merged archive layout. Release archives are
uploaded as workflow artifacts only for `v<version>` tag runs.

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
archives and their `.sha256` sidecars through the GitHub Release job. No
generated release metadata is committed to the repository; the tag CI job is
the sole source of published native artifacts.
