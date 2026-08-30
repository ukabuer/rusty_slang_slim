# Native layer

This directory contains the narrow native build around the pinned Slang C++ API.

The native facade will own all Slang objects and expose only opaque handles, fixed-width enums, byte spans, and explicit create/destroy functions. No Slang COM object or C++ standard-library type will cross the ABI boundary.

The current baseline provides:

- A static, position-independent Slang compiler dependency.
- Unused Slang components disabled before configuration.
- Windows x86_64/MSVC and Android ARM64/API 29 presets.
- The `slang-slim-c-api` static library and `slang-slim-native` meta-target.
- The excluded `slang-slim-feasibility` probe for multi-target codegen and
  reflection.
- The excluded `slang-slim-abi-feasibility` probe for the public C ABI,
  including in-memory virtual files.

The C ABI is declared in `include/slang_slim.h` and implemented in
`src/slang_slim.cpp`. It is intentionally project-owned and is the only native
surface intended for downstream use: callers see opaque compiler/result
handles, borrowed blob views, fixed-width Slang-compatible target and stage
values, generic compiler-option entries, extensible descriptors, and stable
project status codes. The C++ facade and its Slang objects are implementation
details used to provide the
Rust binding; no downstream C++ compatibility promise is made. See
`docs/design.md` for the ownership and source-model decisions.

Calls into Slang are serialized on one worker thread. Results own copied code,
reflection, and diagnostics; VFS-backed Slang sessions are retained for the
worker lifetime to preserve upstream cache safety across repeated compiles.

See `docs/building.md` for bootstrap and build commands.
