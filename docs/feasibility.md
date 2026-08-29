# Native feasibility and size audit

This document records the first native Slang feasibility pass. The probe is a
maintainer-only executable (`slang-slim-feasibility`); it is excluded from the
normal build and is not a crate artifact.

## Results

The probe loads `tests/shaders/multi-entry.hlsl`, composes its vertex,
fragment, and compute entry points once, and creates a session with all three
Windows targets:

| Target | Profile | Result |
| --- | --- | --- |
| HLSL source | `sm_6_0` | 3 source files |
| SPIR-V | `spirv_1_3` | 3 binaries; header version `0x00010300` |
| Metal source | `metallib_2_3` | 3 source files |

Each target also produces a separate JSON reflection document containing all
three entry points. The Metal profile is named `metallib_2_3` in Slang; the
`metal` target selects MSL source output. Android configures only the SPIR-V
target and the ARM64 probe links successfully against API 29.

Commands after the normal configure/bootstrap step:

```powershell
cmake --build --preset windows-x64-feasibility --parallel
build/native/windows-x64/Release/slang-slim-feasibility.exe `
  tests/shaders/multi-entry.hlsl build/feasibility/windows-api

$env:ANDROID_NDK_HOME = (Resolve-Path build/toolchains/android-ndk-r27d).Path
cmake --build --preset android-arm64-feasibility --parallel
```

The Android binary was cross-linked but not executed on a physical device or
emulator in this pass.

## What the official WASM build does

The pinned Slang source's Emscripten preset uses a static library, `-Os`, and
turns off GFX, RHI, CUDA, OptiX, Aftermath, replayer, and tests. The
`slang-wasm` executable then links the compiler and its static dependencies;
the Emscripten linker removes unreachable sections from the final WASM image.
The WASM binding still exposes Slang's ordinary HLSL, SPIR-V, and Metal
formats and serializes reflection JSON, so it is not using a fundamentally
smaller compiler frontend. Its public session helper creates one target at a
time; the Windows multi-target requirement is implemented through the native
API probe here.

The native configuration already disables those components and additionally
disables DXIL/DXC, glslang, slangc, slangd, slangi, slangrt, the proxy,
examples, Dawn/Tint, and mimalloc. Most of those options remove separate
targets or downstream dependencies, not the core compiler passes needed by
HLSL, SPIR-V, Metal, and reflection.

## Size and dependency audit

With Android Release `-g0` (the NDK otherwise injects `-g`), the static archive
set required by the probe is about 63.45 MiB uncompressed and 16.8 MiB in a
ZIP. The main `libslang-compiler.a` is 59.28 MiB; the archive is not standalone
and needs `compiler-core`, `core`, miniz, LZ4, and cmark at final link time.
The equivalent Windows archive set is about 116.98 MiB uncompressed and
26.2 MiB in a ZIP.

The linked Android probe is 30.61 MiB before stripping and about 26 MiB after
`llvm-strip --strip-all`. Its only ELF needed libraries are `libc.so`,
`libdl.so`, and `libm.so`. The Windows probe imports only system DLLs
(`KERNEL32`, `SHELL32`, and `ADVAPI32`) and no Slang, DXC, glslang, or VC
runtime DLL. This confirms that the selected output targets do not require a
runtime downstream compiler/plugin.

## Further trimming decisions

| Candidate | Assessment |
| --- | --- |
| Keep `-g0` for Android Release | Adopted; removes debug sections from every archive at compile time. Keep symbols in a separate maintainer artifact if needed. |
| Link-time GC and strip | Adopt for final wrapper artifacts. This is the native equivalent of the WASM linker's most important size reduction; it cannot shrink the raw static archive itself. |
| Release LTO | Worth a controlled benchmark for the final wrapper (`SLANG_ENABLE_RELEASE_LTO=ON`), but do not require consumers to use LTO when distributing raw static archives. It increases build time and can change link/toolchain requirements. |
| `-Os`/`-Oz` | Worth measuring after the wrapper API exists. It trades compiler throughput for a likely modest image reduction; the WASM result should not be extrapolated directly to Android/Windows. |
| Remove individual Slang source files/backends | Not a stable upstream option and risks hidden reflection/codegen dependencies. Defer unless a map/LTO profile proves a large, isolated win. |
| Disable embedded core-module source | Does not materially reduce the final compiler link in this probe; the source is needed by the host bootstrap path. Keep the current deterministic bootstrap setup. |

The next size experiment should therefore measure a real C ABI wrapper with
Release LTO and platform-appropriate final stripping. The raw archive result
is already suitable for GitHub Release download (and stays outside the
10 MiB crate payload); changing compiler semantics solely to chase the WASM
compressed size would be premature.
