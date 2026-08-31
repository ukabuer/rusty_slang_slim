# Native layer

This directory contains the narrow native build around the pinned Slang C++ API.

The native facade will keep Slang's C++ objects behind opaque handles and
expose Slang-compatible scalar values, descriptor layouts, blob references,
and explicit lifetime functions. No Slang COM vtable or C++ standard-library
type crosses the ABI boundary.

The current baseline provides:

- A static, position-independent Slang compiler dependency.
- Unused Slang components disabled before configuration.
- Windows x86_64/MSVC and Android ARM64/API 29 presets.
- The `slang-slim-c-api` static library and `slang-slim-native` meta-target.
- The excluded `slang-slim-feasibility` probe for multi-target codegen and
  reflection.
- The excluded `slang-slim-abi-feasibility` probe for the public C ABI,
  including in-memory virtual files.

The C ABI is declared in `include/slang_c_api.h` and implemented in
`src/slang_c_api.cpp`. It is intentionally project-owned and is the only native
surface intended for downstream use. The preferred raw declarations use
Slang's own scalar names (`SlangResult`, `SlangCompileTarget`, `SlangStage`,
etc.) and the same sequence as Slang's public API (`GlobalSession`, `Session`,
module and entry-point component types, linking, layout, and blobs). Only the
pointer adaptation and lifetime boundary required to cross a stable C ABI are
added. The C++ facade and its Slang objects are
implementation details used to provide the Rust binding; no downstream C++
compatibility promise is made. See `docs/design.md` for the ownership and
source-model decisions.

The header also exports the first batch of Slang's C++ reflection methods as
stable `slang_reflection_*` C functions. This avoids depending on the
upstream `spReflection_*` declarations in `slang-deprecated.h`. Reflection
records are borrowed from a program layout and are intentionally not released
through a separate ABI. The safe crate wraps these calls as typed views while
retaining the owning component; `ProgramLayout::toJson` remains available for
Slang's JSON representation.

`SlangResult` keeps Slang's HRESULT convention: values below zero are failures,
while zero and positive values are successes. Compilation APIs return warning
and informational text through their diagnostic blob even when the result is
successful; callers should inspect the result sign rather than compare it to
`SLANG_OK`.

Calls into Slang run synchronously on the calling thread and follow Slang's
own synchronization contract; the bridge does not create a worker or add a
thread-affinity policy. Results own the references returned by Slang for code,
reflection, and diagnostics. A VFS handle is retained by Slang's session and
released through the normal COM-style reference counting path.

See `docs/building.md` for bootstrap and build commands.

After a local Release build, the Rust tests and examples can consume the CMake
binary directory directly by setting `SLANG_SLIM_NATIVE_BUILD_DIR`. The Rust
build script then links the known CMake library layout without creating a
release archive; this is a development override only and does not replace the
manifested, checksum-validated release asset.

For a one-command source build, set `SLANG_SLIM_FROM_SOURCE=1`; Cargo invokes
the matching CMake Release preset and then links the resulting native tree.
