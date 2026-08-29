# slang-slim

`slang-slim` is a focused, prebuilt Slang integration for Rust applications that compile HLSL at runtime.

The initial release is intentionally limited to:

- Windows x86_64 MSVC: HLSL Shader Model 6.0 source, SPIR-V 1.3, and MSL 2.3 source.
- Android ARM64 with `minSdk` 29: SPIR-V 1.3 only.
- Vertex, fragment, and compute entry points.
- Multiple entry points in one HLSL translation unit.
- Target-specific reflection JSON and virtual file-system support.

Consumers will download prebuilt native archives from GitHub Releases. Building Slang from source is a maintainer workflow, not part of a consumer `cargo build`.

The repository currently contains only the raw `slang-slim-sys` crate. A safe `slang-slim` crate may be added later.

See [docs/design.md](docs/design.md) for the frozen v0.1 scope and
[docs/building.md](docs/building.md) for maintainer build baselines.
