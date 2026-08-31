# Shader fixtures

`multi-entry.hlsl` contains vertex, fragment, and compute entry points in one
HLSL-compatible Slang translation unit. The native feasibility checks use it to
exercise the three v0.1 output targets and reflection. Virtual includes and
diagnostic fixtures are covered by the project-owned C ABI probe and Rust FFI
smoke test. The fixture documents the release matrix; it is not intended to
define the complete set of Slang inputs or stages accepted by the bridge.
