# Native layer

This directory will contain the narrow C ABI facade over the pinned Slang C++ API.

The native facade will own all Slang objects and expose only opaque handles, fixed-width enums, byte spans, and explicit create/destroy functions. No Slang COM object or C++ standard-library type will cross the ABI boundary.

The next implementation step is a native feasibility spike for:

- Windows x86_64 MSVC with HLSL, SPIR-V, and MSL emitters.
- Android ARM64 with the direct SPIR-V emitter only.
- Static-link feasibility and downstream SPIR-V plugin discovery.
- Release archive size and runtime dependency inspection.

