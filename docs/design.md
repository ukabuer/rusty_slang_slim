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

Reflection is returned as one Slang-generated JSON blob per target. Slang reflection objects are not exposed across the C ABI. A future safe Rust crate may deserialize the JSON into owned Rust data structures without changing the native ABI.

## Virtual file system

The native ABI will support synchronous file-loading callbacks and in-memory virtual files. The C++ facade will adapt these callbacks to Slang's file-system interfaces and will own any blobs passed to Slang.

The design must account for path normalization, unique file identities, `#pragma once`, cache invalidation, and the prohibition on unwinding through C callbacks.

## Native ABI

The ABI will expose only:

- ABI, build, and supported-target queries.
- Opaque compiler and result handles.
- Compilation request descriptors with `struct_size` fields.
- Entry-point, target, define, source, and virtual-file arrays.
- Byte blobs, target-specific reflection JSON, and diagnostics.
- Explicit ownership and destruction functions.

The first native slice implements this surface in `native/include/slang_slim.h`.
It is a project-owned C ABI whose numeric target and stage values, descriptor
field meanings, and default target/session settings intentionally follow the
corresponding Slang API. This keeps the Rust layer close to Slang's model while
avoiding exposure of Slang's C++ ABI. The native C++ facade is an implementation
detail for the Rust binding; downstream C++ source or binary compatibility is
not a supported use case.

Target descriptors accept either the original three convenience identifiers or
SlangCompileTarget-compatible format values with an optional profile name.
Unknown formats are passed through to Slang and reported as unsupported only
when the selected native build cannot provide the requested profile/backend;
capability queries recognize the profile and platform policy, while the
compile result remains authoritative for optional downstream tools.
The platform policy is intentionally small: Android assets accept SPIR-V only;
Windows assets retain the generic target path so future release builds can add
formats without changing the C ABI. `struct_size` prefixes allow newer fields
to be appended without invalidating callers compiled against an older prefix.

Status values are owned by slang-slim rather than exposing Slang's HRESULT-like
codes. A compile result owns all generated blobs and diagnostics; callers only
borrow `slang_slim_blob` views until `slang_slim_compilation_destroy` is called.
Descriptors and their pointed-to data are borrowed for the duration of the
compile call. Virtual-file callbacks are synchronous and receive normalized
UTF-8 paths; returned bytes are copied before the callback returns. Session
search paths, matrix layout, GLSL syntax, effect annotations, and SPIR-V
validation settings and generic compiler-option entries are copied into the
Slang session when the corresponding descriptor suffix is present. Target
descriptors carry the equivalent per-target option array. Option names and
value kinds use the numeric values from Slang's public API, so a newer caller
can pass an option not yet given a slang-slim convenience constant.

Slang work is serialized on a dedicated worker thread; this also keeps
callbacks synchronous without allowing concurrent access to Slang's mutable
linkage state. Generated blobs are copied into the result before the worker
releases its temporary component graph. VFS-backed Slang sessions remain
resident until process exit because the pinned compiler retains cache state
that is not safe to tear down between custom-file-system compilations. The
worker's global session is likewise intentionally kept alive through process
exit; this avoids teardown-order failures in the upstream static build.

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
