# slang-slim v0.1 design

## Goals

`slang-slim` embeds a deliberately narrow subset of the Slang shader compiler behind a stable project-owned C ABI and raw Rust FFI declarations.

Consumer builds must not require CMake, a C++ compiler, bindgen, the Android NDK, or the Slang source tree. Prebuilt native artifacts are downloaded from GitHub Releases and verified before linking.

## Supported hosts and outputs

| Compiler host | Rust target | Outputs |
| --- | --- | --- |
| Windows x64 | `x86_64-pc-windows-msvc` | HLSL SM 6.0 source, SPIR-V 1.3, MSL 2.3 source |
| Android ARM64 | `aarch64-linux-android` | SPIR-V 1.3 |

Android uses `minSdk` 29. Android x86_64 emulator support is outside v0.1.

SPIR-V 1.3 output is validated against the Vulkan 1.1 environment.

## Source and program model

- Input language is strict HLSL.
- Supported stages are vertex, fragment, and compute.
- A translation unit may contain multiple explicitly named entry points.
- One compilation request may compose multiple entry points.
- Windows may generate all three targets from one shared frontend pass when target-specific preprocessing is not required.
- Output code is addressed by `(target, entry_point)`.

## Reflection

Reflection is returned as one Slang-generated JSON blob per target. Slang
reflection objects are not exposed across the C ABI. The safe Rust crate
returns the JSON as owned `Vec<u8>` (or a lossy UTF-8 `String` convenience
method); typed reflection structures can be added later without changing the
native ABI.

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
program-layout reflection blob. These raw handles use `SlangResult`-compatible
return values and return an owned reference to Slang's `ISlangBlob`; the ABI
accessors expose its `getBufferPointer`/`getBufferSize` data without exposing
the C++ vtable. Exported functions use the `slang_` symbol namespace and the
Rust sys crate exposes the same Slang-shaped type and constant names. The
native C++ facade is an implementation detail for the Rust binding.

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
code, reflection, and diagnostics are exposed as owned blob references. When
a custom file system is passed to a session, Slang retains it through its
internal COM-style references (including any cache wrapper) and releases it
when the session/linkage graph is destroyed.

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

The native artifact shape remains intentionally undecided until the packaging
step determines how the C ABI archive and its Slang static dependencies are
published as GitHub Release assets.

## Dependency policy

Slang is included as a recursive git submodule pinned to a stable release commit. A git submodule records an exact commit; it does not automatically follow the latest tag. Upgrades are explicit, reviewed changes that update the submodule pointer and rebuild all release assets.

The initial pin is Slang `v2026.16.1`.

## Distribution

The Rust crate stays small and selects a GitHub Release asset from the crate version and Rust target triple. The crate will embed immutable SHA-256 metadata and support local archives, mirrors, and persistent caches.

Each native asset is named
`slang-slim-native-v{version}-{rust-target}.zip`. It contains the public C
header, the non-LTO static facade and its audited static dependencies, and a
`manifest.json` that fixes library order, platform runtime/system libraries,
file sizes, and per-file SHA-256 hashes. A sibling `.zip.sha256` file records
the checksum that must be embedded in the published Rust crate before release.

The dependencies remain separate archives inside the ZIP. This preserves the
normal linker's selective object extraction and avoids rewriting upstream
archives; consumers still see one downloadable asset. LTO archives are not
published.

Source builds are a maintainer and CI workflow only.

For maintainer iteration, `slang-slim-sys` also accepts the development-only
`SLANG_SLIM_FROM_SOURCE=1` switch. Cargo configures/builds the matching CMake
Release tree and links it directly; `SLANG_SLIM_NATIVE_BUILD_DIR` remains an
explicit override for an already-built tree. Both paths bypass archive and
checksum handling; published consumers continue to use the versioned,
validated native asset flow.
