# slang-slim v0.1 design

## Goals

`slang-slim` embeds a capability-trimmed build of the Slang shader compiler
behind a stable project-owned C ABI and raw Rust FFI declarations. The build
removes optional dependencies and tools; it does not impose a strict-HLSL
parser or a fixed target/stage allow-list on the bridge.

Consumer builds must not require CMake, a C++ compiler, bindgen, the Android NDK, or the Slang source tree. Prebuilt native artifacts are downloaded from GitHub Releases and verified before linking.

## v0.1 distribution and validation matrix

| Compiler host | Rust target | Outputs |
| --- | --- | --- |
| Windows x64 | `x86_64-pc-windows-msvc` | HLSL SM 6.0 source, SPIR-V 1.3, MSL 2.3 source |
| Android ARM64 | `aarch64-linux-android` | SPIR-V 1.3 |

Android uses `minSdk` 29. Android x86_64 emulator support is outside v0.1.

SPIR-V 1.3 output is validated against the Vulkan 1.1 environment. These rows
describe the artifacts published and exercised by v0.1. They are not a claim
that the native compiler rejects every other Slang target or profile; see
[the capability audit](capability-audit.md).

## Source and program model

- `load_module_from_source` delegates to Slang's
  `loadModuleFromSource`. Slang parses the source as its `Slang` language by
  default and selects GLSL compatibility for an apparent `.glsl` path; the
  current bridge has no explicit source-language selector.
- The v0.1 examples and compatibility promise use HLSL-compatible source, but
  the bridge does not enforce a strict-HLSL input policy.
- The raw ABI and safe core wrapper forward all Slang stage values. Vertex,
  fragment, and compute are the only stages covered by the v0.1 tests and
  release promise.
- A translation unit may contain multiple explicitly named entry points.
- One compilation request may compose multiple entry points.
- Target formats and profiles are supplied through Slang-shaped descriptors;
  the current Windows example generates all three v0.1 targets from one shared
  component graph when target-specific preprocessing is not required.
- Output code is addressed by `(target, entry_point)`.

## Reflection

Reflection follows Slang's original `ProgramLayout` flow. The native header
exports the first typed reflection C++ API batch through stable
`slang_reflection_*` C functions, while retaining Slang's opaque record names
and numeric enum values. Reflection records are
borrowed from the target program layout; the ABI does not add retain/release
functions for them. The bridge exposes the underlying `SlangReflection*` from
the existing layout handle, and the safe Rust wrapper keeps the owning layout
and component graph alive while typed child views are in use.

The safe wrapper currently covers program-layout entry points and global
parameters, entry-point layouts, type and type-layout queries, variable
layouts, binding/semantic data, and compute thread-group dimensions. The
methods return the same absence/null semantics as Slang (`Option` in Rust) and
copy strings into owned `String` values. Additional original reflection
functions can be added incrementally without introducing a custom reflection
schema.

JSON remains available through Slang's original C++ `ProgramLayout::toJson`
API. The safe wrapper returns its bytes as an owned `Vec<u8>` and provides a
lossy UTF-8 `String` convenience method; JSON is therefore a Slang format, not
a slang-slim-specific serialization layer.

## Virtual file system

The native ABI supports a synchronous file-loading callback adapter for
Slang's `ISlangFileSystem`. The callback adapter follows Slang's normal blob
ownership contract and does not expose a C++ vtable to Rust.

The design must account for path normalization, unique file identities, `#pragma once`, cache invalidation, and the prohibition on unwinding through C callbacks.

## Native ABI

The ABI exposes only:

- The Slang-shaped global-session/session/component flow, with opaque handles
  for C++ interfaces that cannot cross a stable C ABI.
- Slang-compatible scalar values, descriptors, result codes, and returned
  `ISlangBlob` references.
- The minimum C callback adapter needed to implement `ISlangFileSystem` from
  Rust.

The native slice implements this surface in `native/include/slang_c_api.h`.
It is a project-owned C ABI whose numeric target and stage values, descriptor
field meanings, and default target/session settings intentionally follow the
corresponding Slang API. This keeps the Rust layer close to Slang's model while
avoiding exposure of Slang's C++ ABI. The native C++ facade is an implementation
detail for the Rust binding; downstream C++ source or binary compatibility is
not a supported use case.

The API mirrors Slang's object flow: create a global session,
create a session from `TargetDesc`-shaped records, load a module, find/check
entry points, compose and link component types, then request target code and a
program-layout reflection. JSON is returned as an owned `ISlangBlob`; typed
reflection records are borrowed pointers rooted at that layout. These raw
handles use `SlangResult`-compatible return values and the ABI accessors expose
blob `getBufferPointer`/`getBufferSize` data without exposing the C++ vtable.
Exported functions use the `slang_` symbol namespace for the project-owned
facade, including the `slang_reflection_*` functions that map one-to-one to
Slang's C++ reflection methods. The Rust sys crate exposes the same
Slang-shaped type and constant names. The native C++ facade is an
implementation detail for the Rust binding.

The raw `SlangTargetDesc` takes the upstream `SlangCompileTarget` and
`SlangProfileID` values directly. Unknown formats are passed through to Slang
and reported as unsupported only when the selected native build cannot provide
the requested profile/backend; capability queries recognize the profile and
platform policy, while the compile result remains authoritative for optional
downstream tools.
The platform policy is intentionally small: Android assets accept SPIR-V only;
Windows assets retain the generic target path so future release builds can add
formats without changing the C ABI. The raw descriptors use Slang's
`structureSize` prefixes, allowing newer fields to be appended without
invalidating callers compiled against an older prefix.

The preferred raw API returns Slang's HRESULT-compatible `SlangResult` values
and uses the upstream `SlangCompileTarget`, `SlangStage`, target/session
descriptor fields, and compiler-option numeric values. The C header spells the
namespaced C++ records as top-level C records solely because C has no namespace
syntax. The raw API returns owned `ISlangBlob` references that callers release
through the ABI. Descriptors and their pointed-to data are borrowed for the
duration of the underlying Slang call. Virtual-file callbacks are synchronous
and receive normalized UTF-8 paths; returned blobs follow Slang's normal
ownership contract.

As in Slang, a non-negative `SlangResult` is success; a warning or informational
diagnostic does not turn a successful call into an error. Callers should always
consume the optional diagnostic blob before deciding how to present the result.

Slang calls are synchronous on the calling thread. The bridge does not add a
worker, thread-affinity rule, or process-lifetime session retention: callers
must apply the same synchronization required by Slang's own API. Returned
code and diagnostics, plus JSON reflection, are exposed as owned blob
references; typed reflection children are borrowed views retained by their
program-layout owner. When a custom file system is passed to a session, Slang
retains it through its internal COM-style references (including any cache
wrapper) and releases it when the session/linkage graph is destroyed.

The C ABI only owns the native adapter object. A Rust callback's `userData`
is outside Slang's ownership model, so the safe wrapper keeps that state alive
for at least as long as the native file-system handle can be called. The
wrapper retains the callback state alongside the file-system adapter and
clones that keepalive into each created session; derived components retain the
session. Holding only the opaque native handle is sound for the Slang object
itself, but not for an arbitrary Rust callback context unless that context is
otherwise process-owned.

The safe wrapper keeps native handles in non-`Send`/non-`Sync` Rust owners and
does not introduce a worker or global mutex. Its VFS callback state is
type-erased and retained independently, with `Send + Sync` bounds because the
underlying Slang implementation may invoke the callback from its own execution
context. Successful operations preserve the original non-negative
`SlangResult` and expose warning/informational diagnostics alongside the owned
result bytes.

The native artifact shape is fixed by `scripts/package-native.ps1`: one
deterministic ZIP contains the C ABI header, one merged platform archive, and a
manifest plus sibling SHA-256 file. The merge consumes the facade and audited
Slang dependency archives produced by the Release build; consumers do not need
to know that internal archive layout.

## Dependency policy

Slang is included as a recursive git submodule pinned to a stable release commit. A git submodule records an exact commit; it does not automatically follow the latest tag. Upgrades are explicit, reviewed changes that update the submodule pointer and rebuild all release assets.

The initial pin is Slang `v2026.16.1`.

## Distribution

The Rust crate stays small and selects a GitHub Release asset from the crate
version and Rust target triple. It embeds immutable SHA-256 metadata and
supports local archives, mirrors, and persistent caches. The current index
contains `0.1.0` Windows x64 and Android ARM64 entries; the corresponding ZIPs
are published under the `v0.1.0` GitHub Release path when that release is cut.

Each native asset is named
`slang-slim-native-v{version}-{rust-target}.zip`. It contains the public C
header, the non-LTO static facade and its audited static dependencies, and a
`manifest.json` that fixes the merged library path, platform runtime/system
libraries, file sizes, and per-file SHA-256 hashes. A sibling `.zip.sha256` file records
the checksum copied into the published Rust crate's artifact index.

The dependencies are flattened into the merged archive before packaging. This
keeps one downloadable/linkable library for consumers; object-level extraction
still happens from the merged COFF/AR members at final link time. LTO archives
are not published.

Source builds are a maintainer and CI workflow only.

For maintainer iteration, `slang-slim-sys` also accepts the development-only
`SLANG_SLIM_FROM_SOURCE=1` switch. Cargo configures/builds the matching CMake
Release tree and links it directly; `SLANG_SLIM_NATIVE_BUILD_DIR` remains an
explicit override for an already-built tree. Both paths bypass archive and
checksum handling; published consumers continue to use the versioned,
validated native asset flow.
