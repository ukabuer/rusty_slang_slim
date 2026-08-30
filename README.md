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
native archive is available; ordinary source checks do not require a native
artifact.

See [docs/design.md](docs/design.md) for the frozen v0.1 scope and
[docs/building.md](docs/building.md) for maintainer build baselines.
