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

`.github/workflows/native.yml` is the source-of-truth maintainer workflow. It
runs Rust checks, verifies the pinned Slang gitlink, builds the Windows x64 and
Android ARM64/API 29 Release configurations, runs the Windows ABI executable
smoke test, validates the Android ARM64 ELF link output, compiles the Rust
native tests, and checks the merged archive layout. Successful `main` runs
upload both release archives as workflow artifacts for the release workflow;
pull-request runs still validate the build without uploading artifacts.

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

Pushing a `v<crate-version>` tag starts `.github/workflows/release.yml`. It
waits (up to one hour) for the successful `native.yml` `main` run for the tag's
exact commit, downloads that run's two archives, verifies their ZIP structure
and manifests, and publishes them through the GitHub Release job. GitHub
records the SHA-256 digest for each published asset. Main-build artifacts are
retained for 30 days. No generated release metadata is committed to the
repository.
