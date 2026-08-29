# Maintainer scripts

`bootstrap-android.ps1` configures the Windows x64 build, builds Slang's host
generators, and configures the Android ARM64/API 29 cross build.

`package-native.ps1` copies the audited static-library set into a deterministic
ZIP layout, writes a machine-readable link manifest, and emits a sibling
SHA-256 file. Run it only after the corresponding Release preset succeeds:

```powershell
./scripts/package-native.ps1 `
  -Target x86_64-pc-windows-msvc `
  -Version 0.1.0

./scripts/package-native.ps1 `
  -Target aarch64-linux-android `
  -Version 0.1.0
```

Future scripts will cover release-asset validation.

Consumer `cargo build` must not invoke these scripts.
