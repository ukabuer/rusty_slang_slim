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
    /** Legacy convenience identifiers retained for the first ABI slice. */
    SLANG_SLIM_TARGET_HLSL = 1u,
    SLANG_SLIM_TARGET_SPIRV = 2u,
    SLANG_SLIM_TARGET_METAL = 3u,
};

/** Values mirror SlangCompileTarget so callers can describe a target without
 * being restricted to the three convenience identifiers above. */
typedef uint32_t slang_slim_compile_target;
enum
{
    SLANG_SLIM_COMPILE_TARGET_UNKNOWN = 0u,
    SLANG_SLIM_COMPILE_TARGET_NONE = 1u,
    SLANG_SLIM_COMPILE_TARGET_GLSL = 2u,
    SLANG_SLIM_COMPILE_TARGET_GLSL_VULKAN_DEPRECATED = 3u,
    SLANG_SLIM_COMPILE_TARGET_GLSL_VULKAN_ONE_DESC_DEPRECATED = 4u,
    SLANG_SLIM_COMPILE_TARGET_HLSL = 5u,
    SLANG_SLIM_COMPILE_TARGET_SPIRV = 6u,
    SLANG_SLIM_COMPILE_TARGET_SPIRV_ASM = 7u,
    SLANG_SLIM_COMPILE_TARGET_DXBC = 8u,
    SLANG_SLIM_COMPILE_TARGET_DXBC_ASM = 9u,
    SLANG_SLIM_COMPILE_TARGET_DXIL = 10u,
    SLANG_SLIM_COMPILE_TARGET_DXIL_ASM = 11u,
    SLANG_SLIM_COMPILE_TARGET_C_SOURCE = 12u,
    SLANG_SLIM_COMPILE_TARGET_CPP_SOURCE = 13u,
    SLANG_SLIM_COMPILE_TARGET_HOST_EXECUTABLE = 14u,
    SLANG_SLIM_COMPILE_TARGET_SHADER_SHARED_LIBRARY = 15u,
    SLANG_SLIM_COMPILE_TARGET_SHADER_HOST_CALLABLE = 16u,
    SLANG_SLIM_COMPILE_TARGET_CUDA_SOURCE = 17u,
    SLANG_SLIM_COMPILE_TARGET_PTX = 18u,
    SLANG_SLIM_COMPILE_TARGET_CUDA_OBJECT_CODE = 19u,
    SLANG_SLIM_COMPILE_TARGET_OBJECT_CODE = 20u,
    SLANG_SLIM_COMPILE_TARGET_HOST_CPP_SOURCE = 21u,
    SLANG_SLIM_COMPILE_TARGET_HOST_HOST_CALLABLE = 22u,
    SLANG_SLIM_COMPILE_TARGET_CPP_PYTORCH_BINDING = 23u,
    SLANG_SLIM_COMPILE_TARGET_METAL = 24u,
    SLANG_SLIM_COMPILE_TARGET_METAL_LIB = 25u,
    SLANG_SLIM_COMPILE_TARGET_METAL_LIB_ASM = 26u,
    SLANG_SLIM_COMPILE_TARGET_HOST_SHARED_LIBRARY = 27u,
    SLANG_SLIM_COMPILE_TARGET_WGSL = 28u,
    SLANG_SLIM_COMPILE_TARGET_WGSL_SPIRV_ASM = 29u,
    SLANG_SLIM_COMPILE_TARGET_WGSL_SPIRV = 30u,
    SLANG_SLIM_COMPILE_TARGET_HOST_VM = 31u,
    SLANG_SLIM_COMPILE_TARGET_CPP_HEADER = 32u,
    SLANG_SLIM_COMPILE_TARGET_CUDA_HEADER = 33u,
    SLANG_SLIM_COMPILE_TARGET_HOST_OBJECT_CODE = 34u,
    SLANG_SLIM_COMPILE_TARGET_HOST_LLVM_IR = 35u,
    SLANG_SLIM_COMPILE_TARGET_SHADER_LLVM_IR = 36u,
    SLANG_SLIM_COMPILE_TARGET_COUNT_OF = 37u,
};

typedef uint32_t slang_slim_stage;
enum
{
    /** Stage values mirror SlangStage. */
    SLANG_SLIM_STAGE_NONE = 0u,
    SLANG_SLIM_STAGE_VERTEX = 1u,
    SLANG_SLIM_STAGE_HULL = 2u,
    SLANG_SLIM_STAGE_DOMAIN = 3u,
    SLANG_SLIM_STAGE_GEOMETRY = 4u,
    SLANG_SLIM_STAGE_FRAGMENT = 5u,
    SLANG_SLIM_STAGE_PIXEL = SLANG_SLIM_STAGE_FRAGMENT,
    SLANG_SLIM_STAGE_COMPUTE = 6u,
    SLANG_SLIM_STAGE_RAY_GENERATION = 7u,
    SLANG_SLIM_STAGE_INTERSECTION = 8u,
    SLANG_SLIM_STAGE_ANY_HIT = 9u,
    SLANG_SLIM_STAGE_CLOSEST_HIT = 10u,
    SLANG_SLIM_STAGE_MISS = 11u,
    SLANG_SLIM_STAGE_CALLABLE = 12u,
    SLANG_SLIM_STAGE_MESH = 13u,
    SLANG_SLIM_STAGE_AMPLIFICATION = 14u,
    SLANG_SLIM_STAGE_DISPATCH = 15u,
    SLANG_SLIM_STAGE_NODE = 16u,
    SLANG_SLIM_STAGE_COUNT_OF = 17u,

    /** Accepted only as compatibility aliases for the original v0.1 slice. */
    SLANG_SLIM_STAGE_FRAGMENT_LEGACY = 2u,
    SLANG_SLIM_STAGE_COMPUTE_LEGACY = 3u,
};

typedef uint32_t slang_slim_target_flags;
enum
{
    SLANG_SLIM_TARGET_FLAG_PARAMETER_BLOCKS_USE_REGISTER_SPACES = 1u << 4,
    SLANG_SLIM_TARGET_FLAG_GENERATE_WHOLE_PROGRAM = 1u << 8,
    SLANG_SLIM_TARGET_FLAG_DUMP_IR = 1u << 9,
    SLANG_SLIM_TARGET_FLAG_GENERATE_SPIRV_DIRECTLY = 1u << 10,
    SLANG_SLIM_TARGET_FLAGS_DEFAULT = SLANG_SLIM_TARGET_FLAG_GENERATE_SPIRV_DIRECTLY,
};

typedef uint32_t slang_slim_floating_point_mode;
enum
{
    SLANG_SLIM_FLOATING_POINT_MODE_DEFAULT = 0u,
    SLANG_SLIM_FLOATING_POINT_MODE_FAST = 1u,
    SLANG_SLIM_FLOATING_POINT_MODE_PRECISE = 2u,
};

typedef uint32_t slang_slim_line_directive_mode;
enum
{
    SLANG_SLIM_LINE_DIRECTIVE_MODE_DEFAULT = 0u,
    SLANG_SLIM_LINE_DIRECTIVE_MODE_NONE = 1u,
    SLANG_SLIM_LINE_DIRECTIVE_MODE_STANDARD = 2u,
    SLANG_SLIM_LINE_DIRECTIVE_MODE_GLSL = 3u,
    SLANG_SLIM_LINE_DIRECTIVE_MODE_SOURCE_MAP = 4u,
};

typedef uint32_t slang_slim_matrix_layout_mode;
enum
{
    SLANG_SLIM_MATRIX_LAYOUT_MODE_UNKNOWN = 0u,
    SLANG_SLIM_MATRIX_LAYOUT_ROW_MAJOR = 1u,
    SLANG_SLIM_MATRIX_LAYOUT_COLUMN_MAJOR = 2u,
};

typedef uint32_t slang_slim_session_flags;
enum
{
    SLANG_SLIM_SESSION_FLAGS_NONE = 0u,
};

/** Values mirror slang::CompilerOptionValueKind and CompilerOptionValue. */
typedef uint32_t slang_slim_compiler_option_value_kind;
enum
{
    SLANG_SLIM_COMPILER_OPTION_VALUE_INT = 0u,
    SLANG_SLIM_COMPILER_OPTION_VALUE_STRING = 1u,
};

/** Common CompilerOptionName values retained as convenience constants. The
 * option entry type also accepts numeric values added by newer Slang releases. */
typedef uint32_t slang_slim_compiler_option_name;
enum
{
    SLANG_SLIM_COMPILER_OPTION_MATRIX_LAYOUT_COLUMN = 8u,
    SLANG_SLIM_COMPILER_OPTION_MATRIX_LAYOUT_ROW = 9u,
    SLANG_SLIM_COMPILER_OPTION_EMIT_SPIRV_DIRECTLY = 58u,
    SLANG_SLIM_COMPILER_OPTION_EMIT_REFLECTION_JSON = 110u,
};

typedef struct slang_slim_compiler_option_value
{
    slang_slim_compiler_option_value_kind kind;
    int32_t int_value0;
    int32_t int_value1;
    const char* string_value0;
    const char* string_value1;
} slang_slim_compiler_option_value;

typedef struct slang_slim_compiler_option_entry
{
    /** Numeric value mirrors slang::CompilerOptionName. */
    slang_slim_compiler_option_name name;
    slang_slim_compiler_option_value value;
} slang_slim_compiler_option_entry;

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

    /** Legacy shorthand. Ignored when `format` is non-zero. */
    slang_slim_target target;

    /** SlangCompileTarget-compatible format. A non-zero value opts into the
     * generic target path and allows formats beyond the convenience aliases. */
    slang_slim_compile_target format;

    /** Optional Slang profile name, for example "sm_6_0" or "spirv_1_3".
     * If omitted, slang-slim supplies the legacy default for known formats. */
    const char* profile;

    /** SlangTargetFlags-compatible code-generation flags. */
    slang_slim_target_flags flags;

    /** SlangFloatingPointMode-compatible setting. */
    slang_slim_floating_point_mode floating_point_mode;

    /** SlangLineDirectiveMode-compatible setting. */
    slang_slim_line_directive_mode line_directive_mode;

    /** Set to non-zero to request scalar GLSL buffer layout. */
    uint32_t force_glsl_scalar_buffer_layout;

    /** Optional entries mirroring slang::TargetDesc::compilerOptionEntries. */
    const slang_slim_compiler_option_entry* compiler_options;
    size_t compiler_option_count;
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

    /** Targets are emitted in this order. The platform build determines which
     * formats and profiles are available; query them before compiling. */
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

    /** Optional Slang SessionDesc-compatible settings. Fields are ignored
     * when their containing suffix is absent according to struct_size. */
    const char* const* search_paths;
    size_t search_path_count;
    slang_slim_session_flags session_flags;
    slang_slim_matrix_layout_mode default_matrix_layout_mode;
    uint32_t allow_glsl_syntax;
    uint32_t skip_spirv_validation;
    uint32_t enable_effect_annotations;

    /** Optional entries mirroring slang::SessionDesc::compilerOptionEntries. */
    const slang_slim_compiler_option_entry* compiler_options;
    size_t compiler_option_count;
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

/** Returns 1 when a generic SlangCompileTarget/profile pair is recognized by
 * this build and permitted by its platform policy. An omitted profile uses
 * the known format default; an actual compile remains authoritative for
 * optional backend/tool availability. */
SLANG_SLIM_API int32_t slang_slim_compiler_supports_target_format(
    const slang_slim_compiler* compiler,
    slang_slim_compile_target format,
    const char* profile);

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
SLANG_SLIM_API slang_slim_compile_target slang_slim_compilation_target_format(
    const slang_slim_compilation* compilation,
    size_t target_index);
/** Borrowed profile name; valid until slang_slim_compilation_destroy. */
SLANG_SLIM_API const char* slang_slim_compilation_target_profile(
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
