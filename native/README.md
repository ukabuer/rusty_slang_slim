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
`src/slang_slim.cpp`. It is intentionally project-owned: callers see opaque
compiler/result handles, borrowed blob views, fixed v0.1 targets, and stable
project status codes rather than Slang C++ interfaces. See `docs/design.md` for
the ownership and source-model decisions.

See `docs/building.md` for bootstrap and build commands.
