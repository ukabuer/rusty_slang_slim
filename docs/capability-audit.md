# Capability audit

This document records the difference between the v0.1 release contract, the
capabilities compiled into the native artifact, and the API surface currently
bridged by `slang-slim`. It is an audit of the pinned Slang `v2026.16.1`
source tree, not a promise that every upstream target is supported by every
host platform.

## Executive summary

The README and the original design document describe a deliberately small
*tested and distributed matrix*:

| Release asset | Tested outputs | Tested stages |
| --- | --- | --- |
| Windows x64 MSVC | HLSL source `sm_6_0`, SPIR-V `spirv_1_3`, MSL source `metallib_2_3` | vertex, fragment, compute |
| Android ARM64/API 29 | SPIR-V `spirv_1_3` | vertex, fragment, compute |

Those values are not compiler gates. The native C ABI accepts Slang's target
and stage values, and the safe wrapper forwards them without matching against a
fixed allow-list. The target/profile and stage still have to be meaningful to
the selected Slang build, and the actual compile result is authoritative.

The real reductions are in two different places:

1. The CMake profile removes optional downstream compilers, runtimes, tools,
   and rendering integrations.
2. `slang-slim-sys` exposes a small project-owned C ABI rather than all of
   Slang's public C/C++ API. `slang-slim` is a still smaller safe projection of
   that ABI.

The distinction matters: a target can be present in the upstream enum and in
the bridge while its backend is unavailable in this native build, while a
target can also be usable by the compiler even though it is not listed in the
v0.1 release table.

## Input language behavior

`Session::loadModuleFromSource` is the only source-loading operation currently
bridged. In the pinned Slang implementation,
`Linkage::loadSourceModuleImpl` initializes a translation unit as
`SourceLanguage::Slang`. A path whose apparent extension is `.glsl` selects
Slang's GLSL compatibility mode. The project does not inspect the extension to
reject other source files, and it does not implement a strict-HLSL parser.

Consequently:

- Slang source (`.slang`, or source passed with another non-GLSL path) is the
  natural input for the current API.
- HLSL-compatible source works, which is why the fixtures use `.hlsl`, but it
  is parsed by Slang's frontend rather than by a separately selected HLSL
  frontend.
- GLSL parser/compatibility code remains in the compiler. A `.glsl` path can
  select it, although GLSL-to-SPIR-V and SPIR-V assembly conversions may need
  the optional `slang-glslang`/SPIR-V downstream module that this build does
  not bundle.
- The bridge has no source-language selector and does not expose upstream
  `loadModule`, `loadModuleFromSourceString`, or `loadModuleFromIRBlob`.
  Therefore the presence of `SlangSourceLanguage` values in upstream
  `slang.h` does not mean that all of those input forms are available through
  this crate.

`allowGLSLSyntax` and the global `enableGLSL` fields are passed through as
Slang session fields; they are not an HLSL-only capability switch.

## Output and profile behavior

The C header and Rust `sys` crate retain the upstream `SlangCompileTarget`
numeric values. `find_profile` also delegates to Slang at runtime, so there is
no hard-coded `sm_6_0`, `spirv_1_3`, or `metallib_2_3` check in the bridge.
The pinned profile table contains the following relevant families:

| Target | Profiles present in the pinned Slang source | Native-build status |
| --- | --- | --- |
| `SLANG_HLSL` | `sm_4_0`, `sm_4_1`, `sm_5_0`, `sm_5_1`, `sm_6_0` and `sm_6_1` through `sm_6_10` aliases; stage-specific DX profiles also exist | Native HLSL source emitter is compiled. The v0.1 tests only cover `sm_6_0`. |
| `SLANG_SPIRV` | `spirv_1_0` through `spirv_1_6` | The default target flags request Slang's direct SPIR-V emitter, which is compiled into the artifact. The v0.1 probe validates only 1.3. |
| `SLANG_METAL` | `metallib_2_3`, `metallib_2_4` | Native MSL source emitter is compiled. `SLANG_METAL_LIB` is a separate MetalC downstream path and is not bundled. |
| `SLANG_GLSL` | `glsl_150` through `glsl_460`, including stage profiles | Native GLSL source emitter is compiled. The external glslang wrapper is not part of the release artifact. |
| `SLANG_DXBC` | DX/SM profiles above | On Windows the upstream FXC adapter is compiled and dynamically looks for `d3dcompiler_47`; that system dependency is not packaged or covered by the release tests. |
| `SLANG_WGSL` | No WGSL profile family is defined in the pinned profile table | The native source-code path exists, but WGSL profiles and WGSL-to-SPIR-V via Tint are outside the v0.1 contract. |
| `SLANG_DXIL`, `SLANG_PTX`, host/LLVM/object targets | Upstream enum values exist | These require disabled or external downstream components in this build; see the build cuts below. |

`SLANG_SPIRV_ASM` is not equivalent to direct SPIR-V. Slang first produces a
SPIR-V module and then disassembles it through a downstream SPIR-V tool. Since
the artifact omits `slang-glslang` and bundled SPIR-V tools, that conversion is
not a supported v0.1 capability even though the enum is present.

The upstream `checkCompileTargetSupport` query is also not a complete
compile-capability query for this profile. Its SPIR-V branch checks the
downstream disassembler path and can report unavailable when optional tools are
absent, while the direct SPIR-V emitter still succeeds. Callers should treat a
real compile as authoritative until the bridge exposes a more precise
capability report.

## Stage and entry-point behavior

The C ABI declares every upstream `SlangStage` value (vertex, hull, domain,
geometry, fragment/pixel, compute, ray-tracing stages, mesh, amplification,
dispatch, and node). `slang_module_find_and_check_entry_point` casts the value
straight to Slang, and the safe wrapper does not restrict it. The v/f/c list is
therefore a release/test policy, not a native API limitation.

The component flow accepts an arbitrary number of entry points and component
types. Multiple entry points in one source file and multiple stages in one
composition are not special cases in the bridge; they are ordinary Slang
component composition.

## Native build cuts

The native profile in `native/cmake/SlangSlimOptions.cmake` keeps the core Slang
frontend, source emitters, direct SPIR-V emitter, layout, and reflection code,
but disables these parts of the upstream distribution:

| CMake choice | Effect on actual capability |
| --- | --- |
| `SLANG_ENABLE_DXIL=OFF` | No bundled DXC/DXIL backend; DXIL and DXIL assembly are unavailable. |
| `SLANG_SLANG_LLVM_FLAVOR=DISABLE` | No slang-LLVM module; `HostLLVMIR`, `ShaderLLVMIR`, `HostObjectCode`, and LLVM-selected host-callable/object paths are unavailable. Generic C/C++ source emission is a separate native path. |
| `SLANG_ENABLE_CUDA=OFF`, `SLANG_ENABLE_OPTIX=OFF` | No CUDA/NVRTC or OptiX end-to-end path. |
| `SLANG_ENABLE_SLANG_GLSLANG=OFF` | No `slang-glslang` runtime module and no bundled glslang/SPIR-V Tools downstream path. This does not remove Slang's native GLSL parser or source emitter. |
| `SLANG_EXCLUDE_DAWN=ON`, `SLANG_EXCLUDE_TINT=ON` | No Dawn/Tint WebGPU downstream path. |
| `SLANG_ENABLE_GFX=OFF`, `SLANG_ENABLE_SLANG_RHI=OFF` | No rendering API/RHI integration. Shader source compilation and reflection are independent of those targets. |
| `SLANG_ENABLE_SLANGC/D/I/RT/PROXY=OFF`, tests/examples/replayer off | Command-line tools, interpreter/runtime, compatibility proxy, and upstream test/example executables are not shipped. |

These settings reduce dependencies and distributed targets; they do not
selectively delete the HLSL/Slang parser or force one shader model. Optional
downstream compilers that the host happens to provide (for example Windows
FXC) are still conditional and are not part of the release guarantee.

## API surface compared with upstream Slang

The project-owned C ABI intentionally preserves Slang-shaped records and
opaque ownership, but it is not the official `slang.h` C ABI and cannot expose
C++ vtables. Compared with upstream `IGlobalSession`/`ISession`/`IComponentType`
it currently omits, among other APIs:

- module loading by name, source-string and IR-blob loading, loaded-module
  enumeration, binary-module freshness checks, and compile requests;
- specialization, type-conformance, dynamic-type/RTTI, shared-library-loader,
  downstream-compiler path/default/prelude/version, capability and transition
  queries, and core-module controls;
- most artifact/metadata APIs and the full reflection surface;
- `ISlangFileSystemExt` operations such as unique file identity and path type.

Reflection JSON is still Slang's own `ProgramLayout::toJson` output. The typed
Rust reflection view covers only the first batch of program-layout, entry-point,
type/layout, variable-layout, binding, and compute-size accessors. Missing
reflection methods are an API-surface gap, not evidence that the compiler lacks
the corresponding metadata.

The bridge also has a few deliberate ABI conveniences that are not upstream
behavior (for example, a project-owned zero-byte blob helper and exception
boundaries). Pointer-returning upstream calls whose C++ signature has no
`SlangResult` may return `SLANG_OK` when a non-null object is produced; callers
must consume the separate diagnostics blob. These are semantic/API differences,
not target-language restrictions.

## Distribution limits versus compiler limits

`slang-slim-sys/build.rs` currently distributes only
`x86_64-pc-windows-msvc` and `aarch64-linux-android` (API 29) archives. The
Android package and example select SPIR-V only, while the Windows example
selects the three v0.1 targets. Those are artifact and validation choices. A
different target/profile can be requested from the raw descriptors when the
selected native artifact contains the required backend and any required host
downstream compiler is available.

## Direction recommended by this audit

The project should describe itself as a **capability-trimmed Slang runtime
binding with a small, tested v0.1 distribution profile**, rather than as a
strict-HLSL compiler. The next changes should:

1. Keep the raw C ABI and core safe wrapper generic over Slang targets, profiles,
   stages, and entry-point counts. Put the current v/f/c and three-target
   defaults in examples or an explicitly named convenience profile.
2. Document the native build cuts separately from the tested release matrix.
3. Add capability probes/tests for any additional profile we want to promise
   (for example `sm_5_1`, `spirv_1_6`, `metallib_2_4`, and GLSL source), rather
   than deriving guarantees from enum presence alone.
4. Expand the bridge in upstream order: source/IR/module loading, downstream
   configuration and capability queries, the remaining reflection methods, and
   `ISlangFileSystemExt`.

Until those probes and bridge additions land, the v0.1 table remains the only
cross-platform compatibility promise, while the generic behavior described
above is the actual current code path.
