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
#        define SLANG_C_API __declspec(dllexport)
#    else
#        define SLANG_C_API __declspec(dllimport)
#    endif
#elif defined(__GNUC__) || defined(__clang__)
#    define SLANG_C_API __attribute__((visibility("default")))
#else
#    define SLANG_C_API
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
typedef struct SlangCompileRequest SlangCompileRequest;

/* Opaque reflection records from Slang's C API.  Reflection objects are
 * borrowed from the owning program layout; they are never destroyed through
 * this ABI. */
typedef struct SlangProgramLayout SlangProgramLayout;
typedef struct SlangEntryPointLayout SlangEntryPointLayout;
typedef struct SlangReflectionType SlangReflectionType;
typedef struct SlangReflectionTypeLayout SlangReflectionTypeLayout;
typedef struct SlangReflectionVariable SlangReflectionVariable;
typedef struct SlangReflectionVariableLayout SlangReflectionVariableLayout;

typedef SlangProgramLayout SlangReflection;
typedef SlangEntryPointLayout SlangReflectionEntryPoint;
typedef SlangReflectionVariableLayout SlangReflectionParameter;

typedef uint32_t SlangTypeKind;
enum
{
    SLANG_TYPE_KIND_NONE = 0,
    SLANG_TYPE_KIND_STRUCT = 1,
    SLANG_TYPE_KIND_ARRAY = 2,
    SLANG_TYPE_KIND_MATRIX = 3,
    SLANG_TYPE_KIND_VECTOR = 4,
    SLANG_TYPE_KIND_SCALAR = 5,
    SLANG_TYPE_KIND_CONSTANT_BUFFER = 6,
    SLANG_TYPE_KIND_RESOURCE = 7,
    SLANG_TYPE_KIND_SAMPLER_STATE = 8,
    SLANG_TYPE_KIND_TEXTURE_BUFFER = 9,
    SLANG_TYPE_KIND_SHADER_STORAGE_BUFFER = 10,
    SLANG_TYPE_KIND_PARAMETER_BLOCK = 11,
    SLANG_TYPE_KIND_GENERIC_TYPE_PARAMETER = 12,
    SLANG_TYPE_KIND_INTERFACE = 13,
    SLANG_TYPE_KIND_OUTPUT_STREAM = 14,
    SLANG_TYPE_KIND_MESH_OUTPUT = 15,
    SLANG_TYPE_KIND_SPECIALIZED = 16,
    SLANG_TYPE_KIND_FEEDBACK = 17,
    SLANG_TYPE_KIND_POINTER = 18,
    SLANG_TYPE_KIND_DYNAMIC_RESOURCE = 19,
    SLANG_TYPE_KIND_ENUM = 20,
    SLANG_TYPE_KIND_COUNT = 21,
};

typedef uint32_t SlangScalarType;
enum
{
    SLANG_SCALAR_TYPE_NONE = 0,
    SLANG_SCALAR_TYPE_VOID = 1,
    SLANG_SCALAR_TYPE_BOOL = 2,
    SLANG_SCALAR_TYPE_INT32 = 3,
    SLANG_SCALAR_TYPE_UINT32 = 4,
    SLANG_SCALAR_TYPE_INT64 = 5,
    SLANG_SCALAR_TYPE_UINT64 = 6,
    SLANG_SCALAR_TYPE_FLOAT16 = 7,
    SLANG_SCALAR_TYPE_FLOAT32 = 8,
    SLANG_SCALAR_TYPE_FLOAT64 = 9,
    SLANG_SCALAR_TYPE_INT8 = 10,
    SLANG_SCALAR_TYPE_UINT8 = 11,
    SLANG_SCALAR_TYPE_INT16 = 12,
    SLANG_SCALAR_TYPE_UINT16 = 13,
    SLANG_SCALAR_TYPE_INTPTR = 14,
    SLANG_SCALAR_TYPE_UINTPTR = 15,
    SLANG_SCALAR_TYPE_BFLOAT16 = 16,
    SLANG_SCALAR_TYPE_FLOAT_E4M3 = 17,
    SLANG_SCALAR_TYPE_FLOAT_E5M2 = 18,
};

typedef uint32_t SlangResourceShape;
enum
{
    SLANG_RESOURCE_BASE_SHAPE_MASK = 0x0F,
    SLANG_RESOURCE_NONE = 0x00,
    SLANG_TEXTURE_1D = 0x01,
    SLANG_TEXTURE_2D = 0x02,
    SLANG_TEXTURE_3D = 0x03,
    SLANG_TEXTURE_CUBE = 0x04,
    SLANG_TEXTURE_BUFFER = 0x05,
    SLANG_STRUCTURED_BUFFER = 0x06,
    SLANG_BYTE_ADDRESS_BUFFER = 0x07,
    SLANG_RESOURCE_UNKNOWN = 0x08,
    SLANG_ACCELERATION_STRUCTURE = 0x09,
    SLANG_TEXTURE_SUBPASS = 0x0A,
    SLANG_RESOURCE_EXT_SHAPE_MASK = 0x1F0,
    SLANG_TEXTURE_FEEDBACK_FLAG = 0x10,
    SLANG_TEXTURE_SHADOW_FLAG = 0x20,
    SLANG_TEXTURE_ARRAY_FLAG = 0x40,
    SLANG_TEXTURE_MULTISAMPLE_FLAG = 0x80,
    SLANG_TEXTURE_COMBINED_FLAG = 0x100,
    SLANG_TEXTURE_1D_ARRAY = SLANG_TEXTURE_1D | SLANG_TEXTURE_ARRAY_FLAG,
    SLANG_TEXTURE_2D_ARRAY = SLANG_TEXTURE_2D | SLANG_TEXTURE_ARRAY_FLAG,
    SLANG_TEXTURE_CUBE_ARRAY = SLANG_TEXTURE_CUBE | SLANG_TEXTURE_ARRAY_FLAG,
    SLANG_TEXTURE_2D_MULTISAMPLE = SLANG_TEXTURE_2D | SLANG_TEXTURE_MULTISAMPLE_FLAG,
    SLANG_TEXTURE_2D_MULTISAMPLE_ARRAY =
        SLANG_TEXTURE_2D | SLANG_TEXTURE_MULTISAMPLE_FLAG | SLANG_TEXTURE_ARRAY_FLAG,
    SLANG_TEXTURE_SUBPASS_MULTISAMPLE = SLANG_TEXTURE_SUBPASS | SLANG_TEXTURE_MULTISAMPLE_FLAG,
};

typedef uint32_t SlangResourceAccess;
enum
{
    SLANG_RESOURCE_ACCESS_NONE = 0,
    SLANG_RESOURCE_ACCESS_READ = 1,
    SLANG_RESOURCE_ACCESS_READ_WRITE = 2,
    SLANG_RESOURCE_ACCESS_RASTER_ORDERED = 3,
    SLANG_RESOURCE_ACCESS_APPEND = 4,
    SLANG_RESOURCE_ACCESS_CONSUME = 5,
    SLANG_RESOURCE_ACCESS_WRITE = 6,
    SLANG_RESOURCE_ACCESS_FEEDBACK = 7,
    SLANG_RESOURCE_ACCESS_UNKNOWN = 0x7FFFFFFF,
};

typedef uint32_t SlangParameterCategory;
enum
{
    SLANG_PARAMETER_CATEGORY_NONE = 0,
    SLANG_PARAMETER_CATEGORY_MIXED = 1,
    SLANG_PARAMETER_CATEGORY_CONSTANT_BUFFER = 2,
    SLANG_PARAMETER_CATEGORY_SHADER_RESOURCE = 3,
    SLANG_PARAMETER_CATEGORY_UNORDERED_ACCESS = 4,
    SLANG_PARAMETER_CATEGORY_VARYING_INPUT = 5,
    SLANG_PARAMETER_CATEGORY_VARYING_OUTPUT = 6,
    SLANG_PARAMETER_CATEGORY_SAMPLER_STATE = 7,
    SLANG_PARAMETER_CATEGORY_UNIFORM = 8,
    SLANG_PARAMETER_CATEGORY_DESCRIPTOR_TABLE_SLOT = 9,
    SLANG_PARAMETER_CATEGORY_SPECIALIZATION_CONSTANT = 10,
    SLANG_PARAMETER_CATEGORY_PUSH_CONSTANT_BUFFER = 11,
    SLANG_PARAMETER_CATEGORY_REGISTER_SPACE = 12,
    SLANG_PARAMETER_CATEGORY_GENERIC = 13,
    SLANG_PARAMETER_CATEGORY_RAY_PAYLOAD = 14,
    SLANG_PARAMETER_CATEGORY_HIT_ATTRIBUTES = 15,
    SLANG_PARAMETER_CATEGORY_CALLABLE_PAYLOAD = 16,
    SLANG_PARAMETER_CATEGORY_SHADER_RECORD = 17,
    SLANG_PARAMETER_CATEGORY_EXISTENTIAL_TYPE_PARAM = 18,
    SLANG_PARAMETER_CATEGORY_EXISTENTIAL_OBJECT_PARAM = 19,
    SLANG_PARAMETER_CATEGORY_SUB_ELEMENT_REGISTER_SPACE = 20,
    SLANG_PARAMETER_CATEGORY_SUBPASS = 21,
    SLANG_PARAMETER_CATEGORY_METAL_ARGUMENT_BUFFER_ELEMENT = 22,
    SLANG_PARAMETER_CATEGORY_METAL_ATTRIBUTE = 23,
    SLANG_PARAMETER_CATEGORY_METAL_PAYLOAD = 24,
    SLANG_PARAMETER_CATEGORY_COUNT = 25,
    SLANG_PARAMETER_CATEGORY_METAL_BUFFER = SLANG_PARAMETER_CATEGORY_CONSTANT_BUFFER,
    SLANG_PARAMETER_CATEGORY_METAL_TEXTURE = SLANG_PARAMETER_CATEGORY_SHADER_RESOURCE,
    SLANG_PARAMETER_CATEGORY_METAL_SAMPLER = SLANG_PARAMETER_CATEGORY_SAMPLER_STATE,
    SLANG_PARAMETER_CATEGORY_VERTEX_INPUT = SLANG_PARAMETER_CATEGORY_VARYING_INPUT,
    SLANG_PARAMETER_CATEGORY_FRAGMENT_OUTPUT = SLANG_PARAMETER_CATEGORY_VARYING_OUTPUT,
    SLANG_PARAMETER_CATEGORY_COUNT_V1 = SLANG_PARAMETER_CATEGORY_SUBPASS,
};

typedef uint32_t SlangLayoutRules;
enum
{
    SLANG_LAYOUT_RULES_DEFAULT = 0,
    SLANG_LAYOUT_RULES_METAL_ARGUMENT_BUFFER_TIER_2 = 1,
    SLANG_LAYOUT_RULES_DEFAULT_STRUCTURED_BUFFER = 2,
    SLANG_LAYOUT_RULES_DEFAULT_CONSTANT_BUFFER = 3,
};
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
SLANG_C_API SlangResult slang_create_global_session(
    const SlangGlobalSessionDesc* desc,
    IGlobalSession** out_global_session);
SLANG_C_API void slang_global_session_destroy(IGlobalSession* global_session);
SLANG_C_API const char* slang_global_session_get_build_tag(
    const IGlobalSession* global_session);
SLANG_C_API SlangProfileID slang_global_session_find_profile(
    const IGlobalSession* global_session,
    const char* name);
SLANG_C_API SlangResult slang_global_session_check_compile_target_support(
    const IGlobalSession* global_session,
    SlangCompileTarget target);
SLANG_C_API SlangResult slang_global_session_create_session(
    const IGlobalSession* global_session,
    const SlangSessionDesc* desc,
    ISession** out_session);

/** Create and destroy the virtual-file-system adapter used by SessionDesc. */
SLANG_C_API SlangResult slang_file_system_create(
    const SlangFileSystemDesc* desc,
    ISlangFileSystem** out_file_system);
SLANG_C_API void slang_file_system_destroy(ISlangFileSystem* file_system);

/** The stable-ABI counterpart of Slang's slang_createBlob helper. */
SLANG_C_API SlangResult slang_create_blob(
    const void* data,
    size_t size,
    ISlangBlob** out_blob);

SLANG_C_API void slang_session_destroy(ISession* session);
SLANG_C_API SlangResult slang_session_load_module_from_source(
    ISession* session,
    const char* module_name,
    const char* path,
    ISlangBlob* source,
    ISlangBlob** out_diagnostics,
    IModule** out_module);
SLANG_C_API SlangResult slang_session_create_composite_component_type(
    ISession* session,
    IComponentType* const* component_types,
    SlangInt component_type_count,
    IComponentType** out_component_type,
    ISlangBlob** out_diagnostics);

SLANG_C_API SlangResult slang_module_find_and_check_entry_point(
    IModule* module,
    const char* name,
    SlangStage stage,
    IEntryPoint** out_entry_point,
    ISlangBlob** out_diagnostics);
SLANG_C_API const char* slang_module_get_name(const IModule* module);
SLANG_C_API const char* slang_module_get_file_path(const IModule* module);

SLANG_C_API void slang_component_type_destroy(IComponentType* component_type);
SLANG_C_API SlangResult slang_component_type_link(
    IComponentType* component_type,
    IComponentType** out_linked_component_type,
    ISlangBlob** out_diagnostics);
SLANG_C_API SlangResult slang_component_type_get_target_code(
    IComponentType* component_type,
    SlangInt target_index,
    ISlangBlob** out_code,
    ISlangBlob** out_diagnostics);
SLANG_C_API SlangResult slang_component_type_get_entry_point_code(
    IComponentType* component_type,
    SlangInt entry_point_index,
    SlangInt target_index,
    ISlangBlob** out_code,
    ISlangBlob** out_diagnostics);
SLANG_C_API SlangResult slang_component_type_get_layout(
    IComponentType* component_type,
    SlangInt target_index,
    ProgramLayout** out_layout,
    ISlangBlob** out_diagnostics);

SLANG_C_API void slang_program_layout_destroy(ProgramLayout* layout);

/* Return the underlying Slang reflection pointer borrowed from the program
 * layout.  The program layout handle (and therefore its component owner) must
 * remain alive while any slang_reflection_* function is using the returned
 * value. */
SLANG_C_API SlangReflection* slang_program_layout_get_reflection(ProgramLayout* layout);

/* Stable C exports for the corresponding Slang C++ reflection methods.  The
 * upstream `spReflection_*` declarations live in slang-deprecated.h and are
 * deliberately not used here: these bridge symbols call the C++ API so a
 * future Slang release can remove the deprecated C entry points without
 * changing this ABI. Reflection records remain borrowed from their layout. */
SLANG_C_API SlangResult slang_reflection_to_json(
    SlangReflection* reflection,
    ISlangBlob** out_blob);
SLANG_C_API unsigned slang_reflection_get_parameter_count(SlangReflection* reflection);
SLANG_C_API SlangReflectionParameter* slang_reflection_get_parameter_by_index(
    SlangReflection* reflection,
    unsigned index);
SLANG_C_API SlangUInt slang_reflection_get_entry_point_count(SlangReflection* reflection);
SLANG_C_API SlangReflectionEntryPoint* slang_reflection_get_entry_point_by_index(
    SlangReflection* reflection,
    SlangUInt index);
SLANG_C_API SlangReflectionEntryPoint* slang_reflection_find_entry_point_by_name(
    SlangReflection* reflection,
    const char* name);
SLANG_C_API SlangReflectionType* slang_reflection_find_type_by_name(
    SlangReflection* reflection,
    const char* name);
SLANG_C_API SlangReflectionTypeLayout* slang_reflection_get_type_layout(
    SlangReflection* reflection,
    SlangReflectionType* reflection_type,
    SlangLayoutRules rules);
SLANG_C_API SlangReflectionTypeLayout* slang_reflection_get_global_params_type_layout(
    SlangReflection* reflection);
SLANG_C_API SlangReflectionVariableLayout* slang_reflection_get_global_params_var_layout(
    SlangReflection* reflection);

SLANG_C_API const char* slang_reflection_entry_point_get_name(
    SlangReflectionEntryPoint* entry_point);
SLANG_C_API unsigned slang_reflection_entry_point_get_parameter_count(
    SlangReflectionEntryPoint* entry_point);
SLANG_C_API SlangReflectionVariableLayout*
slang_reflection_entry_point_get_parameter_by_index(
    SlangReflectionEntryPoint* entry_point,
    unsigned index);
SLANG_C_API SlangStage slang_reflection_entry_point_get_stage(
    SlangReflectionEntryPoint* entry_point);
SLANG_C_API void slang_reflection_entry_point_get_compute_thread_group_size(
    SlangReflectionEntryPoint* entry_point,
    SlangUInt axis_count,
    SlangUInt* out_size_along_axis);
SLANG_C_API SlangReflectionVariableLayout* slang_reflection_entry_point_get_var_layout(
    SlangReflectionEntryPoint* entry_point);
SLANG_C_API SlangReflectionVariableLayout* slang_reflection_entry_point_get_result_var_layout(
    SlangReflectionEntryPoint* entry_point);

SLANG_C_API SlangTypeKind slang_reflection_type_get_kind(SlangReflectionType* type);
SLANG_C_API unsigned slang_reflection_type_get_field_count(SlangReflectionType* type);
SLANG_C_API SlangReflectionVariable* slang_reflection_type_get_field_by_index(
    SlangReflectionType* type,
    unsigned index);
SLANG_C_API size_t slang_reflection_type_get_element_count(SlangReflectionType* type);
SLANG_C_API SlangReflectionType* slang_reflection_type_get_element_type(
    SlangReflectionType* type);
SLANG_C_API unsigned slang_reflection_type_get_row_count(SlangReflectionType* type);
SLANG_C_API unsigned slang_reflection_type_get_column_count(SlangReflectionType* type);
SLANG_C_API SlangScalarType slang_reflection_type_get_scalar_type(SlangReflectionType* type);
SLANG_C_API SlangResourceShape slang_reflection_type_get_resource_shape(
    SlangReflectionType* type);
SLANG_C_API SlangResourceAccess slang_reflection_type_get_resource_access(
    SlangReflectionType* type);
SLANG_C_API SlangReflectionType* slang_reflection_type_get_resource_result_type(
    SlangReflectionType* type);
SLANG_C_API const char* slang_reflection_type_get_name(SlangReflectionType* type);

SLANG_C_API SlangReflectionType* slang_reflection_type_layout_get_type(
    SlangReflectionTypeLayout* type);
SLANG_C_API SlangTypeKind slang_reflection_type_layout_get_kind(
    SlangReflectionTypeLayout* type);
SLANG_C_API size_t slang_reflection_type_layout_get_size(
    SlangReflectionTypeLayout* type,
    SlangParameterCategory category);
SLANG_C_API size_t slang_reflection_type_layout_get_stride(
    SlangReflectionTypeLayout* type,
    SlangParameterCategory category);
SLANG_C_API int32_t slang_reflection_type_layout_get_alignment(
    SlangReflectionTypeLayout* type,
    SlangParameterCategory category);
SLANG_C_API uint32_t slang_reflection_type_layout_get_field_count(
    SlangReflectionTypeLayout* type);
SLANG_C_API SlangReflectionVariableLayout* slang_reflection_type_layout_get_field_by_index(
    SlangReflectionTypeLayout* type,
    unsigned index);
SLANG_C_API size_t slang_reflection_type_layout_get_element_stride(
    SlangReflectionTypeLayout* type,
    SlangParameterCategory category);
SLANG_C_API SlangReflectionTypeLayout* slang_reflection_type_layout_get_element_type_layout(
    SlangReflectionTypeLayout* type);
SLANG_C_API SlangReflectionVariableLayout* slang_reflection_type_layout_get_element_var_layout(
    SlangReflectionTypeLayout* type);
SLANG_C_API SlangReflectionVariableLayout* slang_reflection_type_layout_get_container_var_layout(
    SlangReflectionTypeLayout* type);
SLANG_C_API SlangParameterCategory slang_reflection_type_layout_get_parameter_category(
    SlangReflectionTypeLayout* type);
SLANG_C_API SlangMatrixLayoutMode slang_reflection_type_layout_get_matrix_layout_mode(
    SlangReflectionTypeLayout* type);

SLANG_C_API const char* slang_reflection_variable_get_name(SlangReflectionVariable* variable);
SLANG_C_API SlangReflectionType* slang_reflection_variable_get_type(
    SlangReflectionVariable* variable);
SLANG_C_API SlangReflectionVariable* slang_reflection_variable_layout_get_variable(
    SlangReflectionVariableLayout* variable);
SLANG_C_API SlangReflectionTypeLayout* slang_reflection_variable_layout_get_type_layout(
    SlangReflectionVariableLayout* variable);
SLANG_C_API size_t slang_reflection_variable_layout_get_offset(
    SlangReflectionVariableLayout* variable,
    SlangParameterCategory category);
SLANG_C_API size_t slang_reflection_variable_layout_get_space(
    SlangReflectionVariableLayout* variable,
    SlangParameterCategory category);
SLANG_C_API const char* slang_reflection_variable_layout_get_semantic_name(
    SlangReflectionVariableLayout* variable);
SLANG_C_API size_t slang_reflection_variable_layout_get_semantic_index(
    SlangReflectionVariableLayout* variable);
SLANG_C_API SlangStage slang_reflection_variable_layout_get_stage(
    SlangReflectionVariableLayout* variable);
SLANG_C_API unsigned slang_reflection_parameter_get_binding_index(
    SlangReflectionParameter* parameter);
SLANG_C_API unsigned slang_reflection_parameter_get_binding_space(
    SlangReflectionParameter* parameter);

SLANG_C_API void slang_blob_destroy(ISlangBlob* blob);
SLANG_C_API const void* slang_blob_get_buffer_pointer(ISlangBlob* blob);
SLANG_C_API size_t slang_blob_get_buffer_size(ISlangBlob* blob);

SLANG_C_API uint32_t slang_abi_version(void);

#ifdef __cplusplus
}
#endif

#endif
