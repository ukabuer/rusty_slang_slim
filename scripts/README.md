# Maintainer scripts

`bootstrap-android.ps1` configures the Windows x64 build, builds Slang's host
generators, and configures the Android ARM64/API 29 cross build.

Future scripts will cover packaging, checksums, and release validation.

Consumer `cargo build` must not invoke these scripts.
