#ifndef SLANG_SLIM_H
#define SLANG_SLIM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#if defined(_WIN32) && defined(SLANG_SLIM_BUILD_SHARED)
#    if defined(SLANG_SLIM_BUILDING)
#        define SLANG_SLIM_API __declspec(dllexport)
#    else
#        define SLANG_SLIM_API __declspec(dllimport)
#    endif
#elif defined(__GNUC__) || defined(__clang__)
#    define SLANG_SLIM_API __attribute__((visibility("default")))
#else
#    define SLANG_SLIM_API
#endif

#define SLANG_SLIM_ABI_VERSION 1u

typedef int32_t slang_slim_status;

enum
{
    SLANG_SLIM_STATUS_OK = 0,
    SLANG_SLIM_STATUS_INVALID_ARGUMENT = -1,
    SLANG_SLIM_STATUS_OUT_OF_MEMORY = -2,
    SLANG_SLIM_STATUS_COMPILE_ERROR = -3,
    SLANG_SLIM_STATUS_UNSUPPORTED = -4,
    SLANG_SLIM_STATUS_NOT_FOUND = -5,
    SLANG_SLIM_STATUS_IO_ERROR = -6,
    SLANG_SLIM_STATUS_INTERNAL_ERROR = -7,
};

typedef uint32_t slang_slim_target;
enum
{
    SLANG_SLIM_TARGET_HLSL = 1u,
    SLANG_SLIM_TARGET_SPIRV = 2u,
    SLANG_SLIM_TARGET_METAL = 3u,
};

typedef uint32_t slang_slim_stage;
enum
{
    SLANG_SLIM_STAGE_VERTEX = 1u,
    SLANG_SLIM_STAGE_FRAGMENT = 2u,
    SLANG_SLIM_STAGE_COMPUTE = 3u,
};

typedef struct slang_slim_compiler slang_slim_compiler;
typedef struct slang_slim_compilation slang_slim_compilation;

/** A borrowed byte view. The owner remains responsible for the storage. */
typedef struct slang_slim_blob
{
    const uint8_t* data;
    size_t size;
} slang_slim_blob;

typedef struct slang_slim_target_desc
{
    /** Set to sizeof(slang_slim_target_desc), or a larger compatible size. */
    uint32_t struct_size;
    slang_slim_target target;
} slang_slim_target_desc;

typedef struct slang_slim_entry_point_desc
{
    /** Set to sizeof(slang_slim_entry_point_desc), or a larger compatible size. */
    uint32_t struct_size;
    const char* name;
    slang_slim_stage stage;
} slang_slim_entry_point_desc;

typedef struct slang_slim_define_desc
{
    /** Set to sizeof(slang_slim_define_desc), or a larger compatible size. */
    uint32_t struct_size;
    const char* name;
    const char* value;
} slang_slim_define_desc;

typedef struct slang_slim_virtual_file
{
    /** Set to sizeof(slang_slim_virtual_file), or a larger compatible size. */
    uint32_t struct_size;
    const char* path;
    const uint8_t* data;
    size_t size;
} slang_slim_virtual_file;

/**
 * Load a file that is not present in the compile request's virtual_files array.
 *
 * The callback is synchronous. `normalized_path` is a UTF-8 path using `/` as
 * the separator. The implementation copies the returned bytes before the
 * callback returns, so the callback owns the memory in `out_file`.
 */
typedef slang_slim_status (*slang_slim_load_file_fn)(
    void* user_data,
    const char* normalized_path,
    slang_slim_blob* out_file);

typedef struct slang_slim_compile_desc
{
    /** Set to sizeof(slang_slim_compile_desc), or a larger compatible size. */
    uint32_t struct_size;

    /** Optional names used for diagnostics and module identity. */
    const char* module_name;
    const char* source_path;

    /** Strict HLSL source bytes. The input is not required to be NUL-terminated. */
    const uint8_t* source;
    size_t source_size;

    /** Explicitly named vertex, fragment, or compute entry points. */
    const slang_slim_entry_point_desc* entry_points;
    size_t entry_point_count;

    /** Targets are emitted in this order. Windows accepts all three targets;
     * Android accepts only SPIR-V. */
    const slang_slim_target_desc* targets;
    size_t target_count;

    /** Optional preprocessor definitions copied for the duration of the compile. */
    const slang_slim_define_desc* defines;
    size_t define_count;

    /** Optional in-memory files consulted before load_file. */
    const slang_slim_virtual_file* virtual_files;
    size_t virtual_file_count;
    slang_slim_load_file_fn load_file;
    void* load_file_user_data;
} slang_slim_compile_desc;

SLANG_SLIM_API uint32_t slang_slim_abi_version(void);

SLANG_SLIM_API slang_slim_status slang_slim_compiler_create(slang_slim_compiler** out_compiler);
SLANG_SLIM_API void slang_slim_compiler_destroy(slang_slim_compiler* compiler);

/** The returned string is owned by the compiler and remains valid until destroy. */
SLANG_SLIM_API const char* slang_slim_compiler_build_tag(const slang_slim_compiler* compiler);

/** Returns 1 when the target is available in this platform build, otherwise 0. */
SLANG_SLIM_API int32_t slang_slim_compiler_supports_target(
    const slang_slim_compiler* compiler,
    slang_slim_target target);

/**
 * Compile one source translation unit and compose all requested entry points.
 *
 * On a descriptor validation error, `out_compilation` is left null. Once the
 * descriptor is valid, a result handle is returned even when compilation
 * fails, so diagnostics can be inspected. The caller must destroy a returned
 * handle for every status value.
 */
SLANG_SLIM_API slang_slim_status slang_slim_compile(
    const slang_slim_compiler* compiler,
    const slang_slim_compile_desc* desc,
    slang_slim_compilation** out_compilation);

SLANG_SLIM_API void slang_slim_compilation_destroy(slang_slim_compilation* compilation);

SLANG_SLIM_API size_t slang_slim_compilation_target_count(
    const slang_slim_compilation* compilation);
SLANG_SLIM_API size_t slang_slim_compilation_entry_point_count(
    const slang_slim_compilation* compilation);
SLANG_SLIM_API slang_slim_target slang_slim_compilation_target(
    const slang_slim_compilation* compilation,
    size_t target_index);
SLANG_SLIM_API const char* slang_slim_compilation_entry_point_name(
    const slang_slim_compilation* compilation,
    size_t entry_point_index);

/** Generated source or binary. Views remain valid until compilation_destroy. */
SLANG_SLIM_API slang_slim_status slang_slim_compilation_get_code(
    const slang_slim_compilation* compilation,
    size_t target_index,
    size_t entry_point_index,
    slang_slim_blob* out_code);

/** Target-specific reflection JSON. The view excludes a trailing NUL byte. */
SLANG_SLIM_API slang_slim_status slang_slim_compilation_get_reflection_json(
    const slang_slim_compilation* compilation,
    size_t target_index,
    slang_slim_blob* out_json);

/** Diagnostics accumulated during compilation. The view excludes a trailing NUL byte. */
SLANG_SLIM_API slang_slim_status slang_slim_compilation_get_diagnostics(
    const slang_slim_compilation* compilation,
    slang_slim_blob* out_diagnostics);

#ifdef __cplusplus
}
#endif

#endif
