# slang-slim

`slang-slim` is a focused, prebuilt Slang integration for Rust applications that compile HLSL at runtime.

The initial release is intentionally limited to:

- Windows x86_64 MSVC: HLSL Shader Model 6.0 source, SPIR-V 1.3, and MSL 2.3 source.
- Android ARM64 with `minSdk` 29: SPIR-V 1.3 only.
- Vertex, fragment, and compute entry points.
- Multiple entry points in one HLSL translation unit.
- Target-specific reflection JSON and virtual file-system support.

Consumers will download prebuilt native archives from GitHub Releases. Building Slang from source is a maintainer workflow, not part of a consumer `cargo build`.

The repository contains two layers. `slang-slim-sys` exposes the raw stable C
ABI with Slang-shaped records and constants. `slang-slim` adds the safe Rust
object flow, deterministic native-handle cleanup, owned code/diagnostic bytes,
and a Rust virtual file-system callback. The safe layer keeps the same
global-session/session/module/component workflow without adding native worker
threads.

The optional `native-tests` feature enables an integration test when a local
native archive or CMake build is available; ordinary source checks do not
require a native artifact.

The safe wrapper also includes a runnable multi-target example where the main
shader loads another shader through `#include`. It generates HLSL, SPIR-V, and
MSL on Windows, and SPIR-V only on Android. With a local native archive
configured, run it with:

```powershell
$env:SLANG_SLIM_NATIVE_ARCHIVE = `
  "../../build/packages/slang-slim-native-v0.1.0-x86_64-pc-windows-msvc.zip"
cargo build -p slang-slim --features native-tests --example multi_target_compile
.\target\debug\examples\multi_target_compile.exe
```

The archive's sibling `.zip.sha256` file is checked automatically. Without a
native archive or source override, ordinary source checks still work. Native
tests and examples use the matching release entry in
`crates/slang-slim-sys/native-artifacts.json`; set a local archive or mirror
when the corresponding GitHub Release asset is not available yet.

When iterating on the native CMake build itself, set
`SLANG_SLIM_NATIVE_BUILD_DIR` to `build/native/windows-x64` after the Release
preset has completed. This bypasses archive creation, checksum validation, and
downloads, and can be used with the same tests and example. See
[docs/building.md](docs/building.md) for the Android target command.

For the one-command source workflow, set `SLANG_SLIM_FROM_SOURCE=1`; Cargo
will invoke the matching CMake Release build before compiling the Rust target.
This is the recommended maintainer workflow when changing the native bridge or
the pinned Slang source:

```powershell
$env:SLANG_SLIM_FROM_SOURCE = "1"
cargo test --workspace --features native-tests -- --nocapture
cargo build -p slang-slim --features native-tests --example multi_target_compile
& .\target\debug\examples\multi_target_compile.exe
```

See [docs/design.md](docs/design.md) for the frozen v0.1 scope and
[docs/building.md](docs/building.md) for maintainer build baselines.
