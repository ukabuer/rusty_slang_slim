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

The native artifact shape remains intentionally undecided until the feasibility spike determines whether Slang can be shipped as a monolithic static archive without runtime downstream-compiler plugins.

## Dependency policy

Slang is included as a recursive git submodule pinned to a stable release commit. A git submodule records an exact commit; it does not automatically follow the latest tag. Upgrades are explicit, reviewed changes that update the submodule pointer and rebuild all release assets.

The initial pin is Slang `v2026.16.1`.

## Distribution

The Rust crate stays small and selects a GitHub Release asset from the crate version and Rust target triple. The crate will embed immutable SHA-256 metadata and support local archives, mirrors, and persistent caches.

Source builds are a maintainer and CI workflow only.
