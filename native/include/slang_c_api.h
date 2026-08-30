#ifndef SLANG_C_API_H
#define SLANG_C_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#if defined(_WIN32) && defined(SLANG_C_API_BUILD_SHARED)
#    if defined(SLANG_C_API_BUILDING)
#        define SLANG_C_API_API __declspec(dllexport)
#    else
#        define SLANG_C_API_API __declspec(dllimport)
#    endif
#elif defined(__GNUC__) || defined(__clang__)
#    define SLANG_C_API_API __attribute__((visibility("default")))
#else
#    define SLANG_C_API_API
#endif

#define SLANG_C_API_ABI_VERSION 1u

/*
 * Stable Slang-shaped C ABI
 * --------------------------
 *
 * This header projects the subset of Slang's public object model needed by
 * the native bridge. C++ interfaces remain opaque; only scalar values and
 * descriptor layouts cross the stable ABI boundary.
 *
 * The native header is usable from C without including Slang's C++ header. In
 * the native implementation slang.h is included first, so the canonical
 * Slang definitions come from the upstream header instead of being redeclared
 * here.
 */

#ifndef SLANG_H
/* SlangResult follows HRESULT semantics: negative values are failures and
 * zero or positive values are successes. Diagnostics may accompany either a
 * successful or failed operation through an out_diagnostics blob. */
typedef int32_t SlangResult;
typedef int32_t SlangInt32;
typedef uint32_t SlangUInt32;
typedef int64_t SlangInt;
typedef uint64_t SlangUInt;
typedef uint32_t SlangProfileID;
typedef uint32_t SlangProfileIDIntegral;
typedef int32_t SlangCompileTargetIntegral;
typedef int32_t SlangCompileTarget;
typedef uint32_t SlangTargetFlags;
typedef uint32_t SlangFloatingPointModeIntegral;
typedef uint32_t SlangFloatingPointMode;
typedef uint32_t SlangLineDirectiveModeIntegral;
typedef uint32_t SlangLineDirectiveMode;
typedef uint32_t SlangMatrixLayoutModeIntegral;
typedef uint32_t SlangMatrixLayoutMode;
typedef uint32_t SlangStageIntegral;
typedef uint32_t SlangStage;

#    define SLANG_FACILITY_WIN_GENERAL 0
#    define SLANG_FACILITY_WIN_API 7
#    define SLANG_FACILITY_CORE 0x200
#    define SLANG_MAKE_ERROR(fac, code) \
        ((int32_t)(((uint32_t)(fac) << 16) | (uint32_t)(code) | 0x80000000u))
#    define SLANG_OK 0
#    define SLANG_FAIL SLANG_MAKE_ERROR(SLANG_FACILITY_WIN_GENERAL, 0x4005)
#    define SLANG_E_NO_INTERFACE SLANG_MAKE_ERROR(SLANG_FACILITY_WIN_GENERAL, 0x4002)
#    define SLANG_E_NOT_IMPLEMENTED SLANG_MAKE_ERROR(SLANG_FACILITY_WIN_GENERAL, 0x4001)
#    define SLANG_E_INVALID_HANDLE SLANG_MAKE_ERROR(SLANG_FACILITY_WIN_API, 6)
#    define SLANG_E_INVALID_ARG SLANG_MAKE_ERROR(SLANG_FACILITY_WIN_API, 0x57)
#    define SLANG_E_OUT_OF_MEMORY SLANG_MAKE_ERROR(SLANG_FACILITY_WIN_API, 0xe)
#    define SLANG_E_CANNOT_OPEN SLANG_MAKE_ERROR(SLANG_FACILITY_CORE, 4)
#    define SLANG_E_NOT_FOUND SLANG_MAKE_ERROR(SLANG_FACILITY_CORE, 5)
#    define SLANG_E_NOT_AVAILABLE SLANG_MAKE_ERROR(SLANG_FACILITY_CORE, 7)
#    define SLANG_FAILED(status) ((status) < 0)
#    define SLANG_SUCCEEDED(status) ((status) >= 0)

enum
{
    SLANG_TARGET_UNKNOWN = 0,
    SLANG_TARGET_NONE = 1,
    SLANG_GLSL = 2,
    SLANG_GLSL_VULKAN_DEPRECATED = 3,
    SLANG_GLSL_VULKAN_ONE_DESC_DEPRECATED = 4,
    SLANG_HLSL = 5,
    SLANG_SPIRV = 6,
    SLANG_SPIRV_ASM = 7,
    SLANG_DXBC = 8,
    SLANG_DXBC_ASM = 9,
    SLANG_DXIL = 10,
    SLANG_DXIL_ASM = 11,
    SLANG_C_SOURCE = 12,
    SLANG_CPP_SOURCE = 13,
    SLANG_HOST_EXECUTABLE = 14,
    SLANG_SHADER_SHARED_LIBRARY = 15,
    SLANG_SHADER_HOST_CALLABLE = 16,
    SLANG_CUDA_SOURCE = 17,
    SLANG_PTX = 18,
    SLANG_CUDA_OBJECT_CODE = 19,
    SLANG_OBJECT_CODE = 20,
    SLANG_HOST_CPP_SOURCE = 21,
    SLANG_HOST_HOST_CALLABLE = 22,
    SLANG_CPP_PYTORCH_BINDING = 23,
    SLANG_METAL = 24,
    SLANG_METAL_LIB = 25,
    SLANG_METAL_LIB_ASM = 26,
    SLANG_HOST_SHARED_LIBRARY = 27,
    SLANG_WGSL = 28,
    SLANG_WGSL_SPIRV_ASM = 29,
    SLANG_WGSL_SPIRV = 30,
    SLANG_HOST_VM = 31,
    SLANG_CPP_HEADER = 32,
    SLANG_CUDA_HEADER = 33,
    SLANG_HOST_OBJECT_CODE = 34,
    SLANG_HOST_LLVM_IR = 35,
    SLANG_SHADER_LLVM_IR = 36,
    SLANG_TARGET_COUNT_OF = 37,

    SLANG_TARGET_FLAG_PARAMETER_BLOCKS_USE_REGISTER_SPACES = 1u << 4,
    SLANG_TARGET_FLAG_GENERATE_WHOLE_PROGRAM = 1u << 8,
    SLANG_TARGET_FLAG_DUMP_IR = 1u << 9,
    SLANG_TARGET_FLAG_GENERATE_SPIRV_DIRECTLY = 1u << 10,

    SLANG_FLOATING_POINT_MODE_DEFAULT = 0,
    SLANG_FLOATING_POINT_MODE_FAST = 1,
    SLANG_FLOATING_POINT_MODE_PRECISE = 2,

    SLANG_LINE_DIRECTIVE_MODE_DEFAULT = 0,
    SLANG_LINE_DIRECTIVE_MODE_NONE = 1,
    SLANG_LINE_DIRECTIVE_MODE_STANDARD = 2,
    SLANG_LINE_DIRECTIVE_MODE_GLSL = 3,
    SLANG_LINE_DIRECTIVE_MODE_SOURCE_MAP = 4,

    SLANG_MATRIX_LAYOUT_MODE_UNKNOWN = 0,
    SLANG_MATRIX_LAYOUT_ROW_MAJOR = 1,
    SLANG_MATRIX_LAYOUT_COLUMN_MAJOR = 2,

    SLANG_STAGE_NONE = 0,
    SLANG_STAGE_VERTEX = 1,
    SLANG_STAGE_HULL = 2,
    SLANG_STAGE_DOMAIN = 3,
    SLANG_STAGE_GEOMETRY = 4,
    SLANG_STAGE_FRAGMENT = 5,
    SLANG_STAGE_COMPUTE = 6,
    SLANG_STAGE_RAY_GENERATION = 7,
    SLANG_STAGE_INTERSECTION = 8,
    SLANG_STAGE_ANY_HIT = 9,
    SLANG_STAGE_CLOSEST_HIT = 10,
    SLANG_STAGE_MISS = 11,
    SLANG_STAGE_CALLABLE = 12,
    SLANG_STAGE_MESH = 13,
    SLANG_STAGE_AMPLIFICATION = 14,
    SLANG_STAGE_DISPATCH = 15,
    SLANG_STAGE_NODE = 16,
    SLANG_STAGE_COUNT = 17,
    SLANG_STAGE_PIXEL = SLANG_STAGE_FRAGMENT,
    SLANG_PROFILE_UNKNOWN = 0,
};

#    ifndef SLANG_API_VERSION
#        define SLANG_API_VERSION 0
#    endif
#    ifndef SLANG_LANGUAGE_VERSION_UNKNOWN
#        define SLANG_LANGUAGE_VERSION_UNKNOWN 0
#    endif
#    ifndef SLANG_LANGUAGE_VERSION_LEGACY
#        define SLANG_LANGUAGE_VERSION_LEGACY 2018
#    endif
#    ifndef SLANG_LANGUAGE_VERSION_202A
#        define SLANG_LANGUAGE_VERSION_202A 2025
#    endif
#    ifndef SLANG_LANGUAGE_VERSION_2025
#        define SLANG_LANGUAGE_VERSION_2025 2025
#    endif
#    ifndef SLANG_LANGUAGE_VERSION_202B
#        define SLANG_LANGUAGE_VERSION_202B 2026
#    endif
#    ifndef SLANG_LANGUAGE_VERSION_2026
#        define SLANG_LANGUAGE_VERSION_2026 2026
#    endif
#    ifndef SLANG_LANGUAGE_VERSION_202C
#        define SLANG_LANGUAGE_VERSION_202C 2027
#    endif
#    ifndef SLANG_LANGUAGE_VERSION_DEFAULT
#        define SLANG_LANGUAGE_VERSION_DEFAULT SLANG_LANGUAGE_VERSION_LEGACY
#    endif
#    ifndef SLANG_LANGUAGE_VERSION_LATEST
#        define SLANG_LANGUAGE_VERSION_LATEST SLANG_LANGUAGE_VERSION_2026
#    endif
#    ifndef SLANG_LANGUAGE_VERSION_NEXT
#        define SLANG_LANGUAGE_VERSION_NEXT SLANG_LANGUAGE_VERSION_202C
#    endif

typedef struct SlangGlobalSessionDesc
{
    uint32_t structureSize;
    uint32_t apiVersion;
    uint32_t minLanguageVersion;
    uint8_t enableGLSL;
    uint8_t _enableGLSLPadding[3];
    uint32_t reserved[16];
} SlangGlobalSessionDesc;

typedef struct ISlangFileSystem ISlangFileSystem;
typedef struct ISlangBlob ISlangBlob;
#endif

/* `slang::IBlob` is an alias of the global `ISlangBlob` upstream. */
typedef ISlangBlob IBlob;

/* These records live in namespace `slang` in C++. They are spelled as
 * top-level C records only because C has no namespace syntax. Their layout is
 * kept identical to the corresponding upstream records. */
typedef uint32_t SessionFlags;
enum
{
    kSessionFlags_None = 0,
};

typedef int32_t CompilerOptionName;
typedef int32_t CompilerOptionValueKind;
enum
{
    COMPILER_OPTION_VALUE_INT = 0,
    COMPILER_OPTION_VALUE_STRING = 1,
};

typedef struct CompilerOptionValue
{
    CompilerOptionValueKind kind;
    int32_t intValue0;
    int32_t intValue1;
    const char* stringValue0;
    const char* stringValue1;
} CompilerOptionValue;

typedef struct CompilerOptionEntry
{
    CompilerOptionName name;
    CompilerOptionValue value;
} CompilerOptionEntry;

typedef struct SlangTargetDesc
{
    size_t structureSize;
    SlangCompileTarget format;
    SlangProfileID profile;
    SlangTargetFlags flags;
    SlangFloatingPointMode floatingPointMode;
    SlangLineDirectiveMode lineDirectiveMode;
    uint8_t forceGLSLScalarBufferLayout;
    uint8_t _forceGLSLScalarBufferLayoutPadding[3];
    const CompilerOptionEntry* compilerOptionEntries;
    uint32_t compilerOptionEntryCount;
} SlangTargetDesc;

typedef struct SlangPreprocessorMacroDesc
{
    const char* name;
    const char* value;
} SlangPreprocessorMacroDesc;

typedef struct SlangSessionDesc
{
    size_t structureSize;
    const SlangTargetDesc* targets;
    SlangInt targetCount;
    SessionFlags flags;
    SlangMatrixLayoutMode defaultMatrixLayoutMode;
    const char* const* searchPaths;
    SlangInt searchPathCount;
    const SlangPreprocessorMacroDesc* preprocessorMacros;
    SlangInt preprocessorMacroCount;
    ISlangFileSystem* fileSystem;
    uint8_t enableEffectAnnotations;
    uint8_t allowGLSLSyntax;
    uint8_t _sessionBoolPadding[6];
    const CompilerOptionEntry* compilerOptionEntries;
    uint32_t compilerOptionEntryCount;
    uint8_t skipSPIRVValidation;
    uint8_t _skipSPIRVValidationPadding[3];
} SlangSessionDesc;

/* Descriptor pointers and strings are borrowed for the duration of
 * slang_global_session_create_session; Slang copies the session settings it
 * retains after that call. */

/* C projections of the records that live in namespace `slang` upstream. */
typedef SlangTargetDesc TargetDesc;
typedef SlangPreprocessorMacroDesc PreprocessorMacroDesc;
typedef SlangSessionDesc SessionDesc;

/**
 * Callback-shaped projection of ISlangFileSystem::loadFile. Slang invokes
 * this synchronously while loading a module; the adapter does not own
 * `userData`, so its owner must outlive every Slang object that can retain the
 * file-system handle.
 */
typedef SlangResult (*SlangLoadFileFunc)(
    void* userData,
    const char* path,
    ISlangBlob** outBlob);

/** ABI-only adapter description for an ISlangFileSystem implementation. */
typedef struct SlangFileSystemDesc
{
    size_t structureSize;
    SlangLoadFileFunc loadFile;
    void* loadFileUserData;
} SlangFileSystemDesc;

/* C has no namespace syntax for the upstream interfaces. These opaque
 * declarations preserve their names and ownership boundaries without exposing
 * C++ vtables. */
typedef struct GlobalSession IGlobalSession;
typedef struct Session ISession;
typedef struct ComponentType IComponentType;
typedef IComponentType IModule;
typedef IComponentType IEntryPoint;
typedef struct ProgramLayout ProgramLayout;

/** Create and destroy a reusable global session. */
SLANG_C_API_API SlangResult slang_create_global_session(
    const SlangGlobalSessionDesc* desc,
    IGlobalSession** out_global_session);
SLANG_C_API_API void slang_global_session_destroy(IGlobalSession* global_session);
SLANG_C_API_API const char* slang_global_session_get_build_tag(
    const IGlobalSession* global_session);
SLANG_C_API_API SlangProfileID slang_global_session_find_profile(
    const IGlobalSession* global_session,
    const char* name);
SLANG_C_API_API SlangResult slang_global_session_check_compile_target_support(
    const IGlobalSession* global_session,
    SlangCompileTarget target);
SLANG_C_API_API SlangResult slang_global_session_create_session(
    const IGlobalSession* global_session,
    const SlangSessionDesc* desc,
    ISession** out_session);

/** Create and destroy the virtual-file-system adapter used by SessionDesc. */
SLANG_C_API_API SlangResult slang_file_system_create(
    const SlangFileSystemDesc* desc,
    ISlangFileSystem** out_file_system);
SLANG_C_API_API void slang_file_system_destroy(ISlangFileSystem* file_system);

/** The stable-ABI counterpart of Slang's slang_createBlob helper. */
SLANG_C_API_API SlangResult slang_create_blob(
    const void* data,
    size_t size,
    ISlangBlob** out_blob);

SLANG_C_API_API void slang_session_destroy(ISession* session);
SLANG_C_API_API SlangResult slang_session_load_module_from_source(
    ISession* session,
    const char* module_name,
    const char* path,
    ISlangBlob* source,
    ISlangBlob** out_diagnostics,
    IModule** out_module);
SLANG_C_API_API SlangResult slang_session_create_composite_component_type(
    ISession* session,
    IComponentType* const* component_types,
    SlangInt component_type_count,
    IComponentType** out_component_type,
    ISlangBlob** out_diagnostics);

SLANG_C_API_API SlangResult slang_module_find_and_check_entry_point(
    IModule* module,
    const char* name,
    SlangStage stage,
    IEntryPoint** out_entry_point,
    ISlangBlob** out_diagnostics);
SLANG_C_API_API const char* slang_module_get_name(const IModule* module);
SLANG_C_API_API const char* slang_module_get_file_path(const IModule* module);

SLANG_C_API_API void slang_component_type_destroy(IComponentType* component_type);
SLANG_C_API_API SlangResult slang_component_type_link(
    IComponentType* component_type,
    IComponentType** out_linked_component_type,
    ISlangBlob** out_diagnostics);
SLANG_C_API_API SlangResult slang_component_type_get_target_code(
    IComponentType* component_type,
    SlangInt target_index,
    ISlangBlob** out_code,
    ISlangBlob** out_diagnostics);
SLANG_C_API_API SlangResult slang_component_type_get_entry_point_code(
    IComponentType* component_type,
    SlangInt entry_point_index,
    SlangInt target_index,
    ISlangBlob** out_code,
    ISlangBlob** out_diagnostics);
SLANG_C_API_API SlangResult slang_component_type_get_layout(
    IComponentType* component_type,
    SlangInt target_index,
    ProgramLayout** out_layout,
    ISlangBlob** out_diagnostics);

SLANG_C_API_API void slang_program_layout_destroy(ProgramLayout* layout);
SLANG_C_API_API SlangResult slang_program_layout_to_json(
    ProgramLayout* layout,
    ISlangBlob** out_json);

SLANG_C_API_API void slang_blob_destroy(ISlangBlob* blob);
SLANG_C_API_API const void* slang_blob_get_buffer_pointer(ISlangBlob* blob);
SLANG_C_API_API size_t slang_blob_get_buffer_size(ISlangBlob* blob);

SLANG_C_API_API uint32_t slang_abi_version(void);

#ifdef __cplusplus
}
#endif

#endif
