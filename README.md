# slang-slim

`slang-slim` is a capability-trimmed, prebuilt Slang integration for Rust
applications that compile shader code at runtime.

The initial release has the following tested and distributed matrix:

- Windows x86_64 MSVC: HLSL Shader Model 6.0 source, SPIR-V 1.3, and MSL 2.3 source.
- Android ARM64 with `minSdk` 29: SPIR-V 1.3 only.
- Vertex, fragment, and compute entry points.
- Multiple entry points in one HLSL translation unit.
- Target-specific Slang reflection (typed views plus JSON) and virtual
  file-system support.

This is a release and validation profile, not a hard compiler allow-list. The
raw descriptors and safe core wrapper forward Slang target/profile/stage values
without restricting them to the entries above. The pinned native build still
omits optional downstream compilers and several upstream API families. See
[the capability audit](docs/capability-audit.md) for the actual boundary
between Slang, the native build, and the Rust bridge.

Consumers will download prebuilt native archives from GitHub Releases. Building Slang from source is a maintainer workflow, not part of a consumer `cargo build`.

The repository contains two layers. `slang-slim-sys` exposes the raw stable C
ABI with Slang-shaped records and constants. `slang-slim` adds the safe Rust
object flow, deterministic native-handle cleanup, owned code/diagnostic bytes,
and a Rust virtual file-system callback. The safe layer keeps the same
global-session/session/module/component workflow without adding native worker
threads.

The optional `native-tests` feature enables an integration test when a local
native archive or extracted package is available; ordinary source checks do not
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

For published assets, the build script derives the GitHub Release URL from the
crate version and target, queries the Release API for that asset's SHA-256
`digest`, and verifies the downloaded archive before extraction. The digest
metadata is cached with the archive. A local `SLANG_SLIM_NATIVE_ARCHIVE` is hashed
directly, so no sidecar file is needed. Set `SLANG_SLIM_NATIVE_SHA256` when using
a custom release mirror that does not expose the GitHub Release API.

For a one-command maintainer build from the checked-out Slang source, set
`SLANG_SLIM_FROM_SOURCE=1`. Cargo configures the matching CMake preset when
needed, builds the Release native target, and links its local libraries directly;
no archive download or GitHub API query is performed. The source mode takes
precedence over `SLANG_SLIM_NATIVE_ARCHIVE` and `SLANG_SLIM_NATIVE_DIR`.
Android source builds additionally accept the bundled
`build/toolchains/android-ndk-r27d` NDK, or use `ANDROID_NDK_HOME`/
`ANDROID_NDK_ROOT` when the NDK is installed elsewhere; the Android path is
currently intended for a Windows host, matching CI.

```powershell
$env:SLANG_SLIM_FROM_SOURCE = "1"
cargo test --workspace --features native-tests -- --nocapture
cargo build -p slang-slim --features native-tests --example multi_target_compile
& .\target\debug\examples\multi_target_compile.exe
Remove-Item Env:SLANG_SLIM_FROM_SOURCE
```

See [docs/design.md](docs/design.md) for the frozen v0.1 scope and
[docs/building.md](docs/building.md) for maintainer build baselines.
