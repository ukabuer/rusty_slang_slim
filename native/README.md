# Native layer

This directory contains the narrow native build around the pinned Slang C++ API.

The native facade will own all Slang objects and expose only opaque handles, fixed-width enums, byte spans, and explicit create/destroy functions. No Slang COM object or C++ standard-library type will cross the ABI boundary.

The current baseline provides:

- A static, position-independent Slang compiler dependency.
- Unused Slang components disabled before configuration.
- Windows x86_64/MSVC and Android ARM64/API 29 presets.
- The `slang-slim-native` build target.

The exported C ABI and ABI-level tests remain deferred until the native
feasibility build confirms the final archive shape. See `docs/building.md` for
bootstrap and build commands.
