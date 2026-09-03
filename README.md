# slang-slim

`slang-slim` provides Rust bindings for a capability-trimmed, prebuilt [Slang](https://github.com/shader-slang/slang)
shader compiler. The safe crate follows Slang's normal workflow: create a
global session, create a compilation session, load a module, compose entry
points, link, and retrieve target code or reflection.

The tested release profile is:

| Platform | Tested outputs | Stages |
| --- | --- | --- |
| Windows x86_64 MSVC | HLSL SM 6.0, SPIR-V 1.3, MSL 2.3 source | vertex, fragment, compute |
| Android ARM64, API 29 | SPIR-V 1.3 | vertex, fragment, compute |

Multiple entry points can be kept in one source file. The raw API forwards
Slang target, profile, and stage values without turning this tested matrix into
a hard compiler allow-list. See the [capability audit](docs/capability-audit.md)
for the difference between the tested release profile and the underlying Slang
build.

## Installation

```console
cargo add slang-slim
```

The crate requires Rust 1.85 or newer.

The default feature selects the prebuilt native asset for the Rust target. It is
downloaded from the matching GitHub Release during the first build; consumers
do not need CMake, a C++ compiler, the Android NDK, or the Slang source tree.
The published native assets currently cover `x86_64-pc-windows-msvc` and
`aarch64-linux-android`.

For the Slang-shaped raw FFI layer instead:

```console
cargo add slang-slim-sys
```

The raw layer can also be added without native linking:

```console
cargo add slang-slim-sys --no-default-features
```

## Quick start

This example compiles one compute entry point to SPIR-V 1.3 and reads its
reflection JSON. `Output<T>` also carries warning and informational diagnostics
returned by Slang.

```rust
use slang_slim::{GlobalSession, SessionDesc, TargetDesc, sys};

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let global = GlobalSession::new()?;
    let profile = global
        .find_profile("spirv_1_3")
        .expect("the native build does not provide spirv_1_3");

    let mut session_desc = SessionDesc::new();
    session_desc.add_target(TargetDesc::new(sys::SLANG_SPIRV, profile));
    let session = global.create_session(&session_desc)?;

    let module = session
        .load_module_from_source(
            "compute_example",
            "compute_example.hlsl",
            br#"[numthreads(1, 1, 1)]
void compute_main(uint3 dispatchThreadId : SV_DispatchThreadID) {}
"#,
        )?
        .value;
    let entry_point = module
        .find_and_check_entry_point("compute_main", sys::SLANG_STAGE_COMPUTE)?
        .value;
    let composite = session
        .create_composite_component_type(&[&entry_point])?
        .value;
    let linked = composite.link()?.value;

    let spirv = linked.get_target_code(0)?.value;
    let reflection = linked.get_layout(0)?.value.to_json_string()?.value;
    println!("SPIR-V: {} bytes; reflection: {} bytes", spirv.len(), reflection.len());
    Ok(())
}
```

Compilation errors are returned as `Error` values with their Slang status and
diagnostics. Successful operations may also contain warnings in
`Output::diagnostics`. To serve `#include` files from memory, implement the
safe `FileSystem` callback and attach it with `SessionDesc::set_file_system`; a
complete multi-entry-point include example is in
[`multi_target_compile.rs`](crates/slang-slim/examples/multi_target_compile.rs).

## Project layout

- `slang-slim-sys` is the raw stable C ABI binding.
- `slang-slim` is the safe Rust object and virtual-file-system wrapper.
- The native artifact contains the project C ABI and the trimmed Slang static
  link set; it is distributed separately from the crates.io packages.

For maintainer build, packaging, and release instructions, see
[`docs/building.md`](docs/building.md). The API and ownership decisions are in
[`docs/design.md`](docs/design.md); native-layer notes are in
[`native/README.md`](native/README.md).
