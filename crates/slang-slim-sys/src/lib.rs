//! Raw FFI declarations for the project-owned native Slang C ABI.
//!
//! The native bridge keeps Slang's C++ interfaces opaque and exposes the
//! upstream scalar values and descriptor layouts directly. Native artifact
//! selection and linker directives remain in `build.rs`, so this crate can be
//! checked before a release asset is available locally.
//!
//! Calls are synchronous on the caller's thread; Slang's own synchronization
//! requirements apply. A non-negative `SlangResult` is success, and optional
//! diagnostic blobs may still contain warnings or informational messages.

#![no_std]

use core::ffi::{c_char, c_void};

/// Version of the stable C ABI exported by the native artifact.
pub const ABI_VERSION: u32 = 1;
pub const SLANG_C_API_ABI_VERSION: u32 = ABI_VERSION;

/// SlangResult uses HRESULT semantics: negative values indicate failure;
/// zero and positive values indicate success. Diagnostics can be returned for
/// either result through the diagnostic blob out-parameters.
pub type SlangResult = i32;
pub const SLANG_OK: SlangResult = 0;
pub const SLANG_FAIL: SlangResult = -2_147_467_259;
pub const SLANG_E_NO_INTERFACE: SlangResult = -2_147_467_262;
pub const SLANG_E_NOT_IMPLEMENTED: SlangResult = -2_147_467_263;
pub const SLANG_E_INVALID_HANDLE: SlangResult = -2_147_024_890;
pub const SLANG_E_INVALID_ARG: SlangResult = -2_147_024_809;
pub const SLANG_E_OUT_OF_MEMORY: SlangResult = -2_147_024_882;
pub const SLANG_E_CANNOT_OPEN: SlangResult = -2_113_929_212;
pub const SLANG_E_NOT_FOUND: SlangResult = -2_113_929_211;
pub const SLANG_E_NOT_AVAILABLE: SlangResult = -2_113_929_209;

pub type SlangInt = i64;
pub type SlangUInt = u64;
pub type SlangInt32 = i32;
pub type SlangUInt32 = u32;
pub type SlangProfileID = u32;
pub type SlangProfileIDIntegral = u32;
pub type SlangCompileTargetIntegral = i32;
pub type SlangCompileTarget = i32;
pub type SlangStageIntegral = u32;
pub type SlangStage = u32;
pub type SlangTargetFlags = u32;
pub type SlangFloatingPointModeIntegral = u32;
pub type SlangFloatingPointMode = u32;
pub type SlangLineDirectiveModeIntegral = u32;
pub type SlangLineDirectiveMode = u32;
pub type SlangMatrixLayoutModeIntegral = u32;
pub type SlangMatrixLayoutMode = u32;
pub type SessionFlags = u32;

pub const SLANG_API_VERSION: u32 = 0;
pub const SLANG_LANGUAGE_VERSION_UNKNOWN: u32 = 0;
pub const SLANG_LANGUAGE_VERSION_LEGACY: u32 = 2018;
pub const SLANG_LANGUAGE_VERSION_202A: u32 = 2025;
pub const SLANG_LANGUAGE_VERSION_2025: u32 = 2025;
pub const SLANG_LANGUAGE_VERSION_202B: u32 = 2026;
pub const SLANG_LANGUAGE_VERSION_2026: u32 = 2026;
pub const SLANG_LANGUAGE_VERSION_202C: u32 = 2027;
pub const SLANG_LANGUAGE_VERSION_DEFAULT: u32 = SLANG_LANGUAGE_VERSION_LEGACY;
pub const SLANG_LANGUAGE_VERSION_LATEST: u32 = SLANG_LANGUAGE_VERSION_2026;
pub const SLANG_LANGUAGE_VERSION_NEXT: u32 = SLANG_LANGUAGE_VERSION_202C;

pub const SLANG_PROFILE_UNKNOWN: SlangProfileID = 0;

pub const SLANG_TARGET_UNKNOWN: SlangCompileTarget = 0;
pub const SLANG_TARGET_NONE: SlangCompileTarget = 1;
pub const SLANG_GLSL: SlangCompileTarget = 2;
pub const SLANG_GLSL_VULKAN_DEPRECATED: SlangCompileTarget = 3;
pub const SLANG_GLSL_VULKAN_ONE_DESC_DEPRECATED: SlangCompileTarget = 4;
pub const SLANG_HLSL: SlangCompileTarget = 5;
pub const SLANG_SPIRV: SlangCompileTarget = 6;
pub const SLANG_SPIRV_ASM: SlangCompileTarget = 7;
pub const SLANG_DXBC: SlangCompileTarget = 8;
pub const SLANG_DXBC_ASM: SlangCompileTarget = 9;
pub const SLANG_DXIL: SlangCompileTarget = 10;
pub const SLANG_DXIL_ASM: SlangCompileTarget = 11;
pub const SLANG_C_SOURCE: SlangCompileTarget = 12;
pub const SLANG_CPP_SOURCE: SlangCompileTarget = 13;
pub const SLANG_HOST_EXECUTABLE: SlangCompileTarget = 14;
pub const SLANG_SHADER_SHARED_LIBRARY: SlangCompileTarget = 15;
pub const SLANG_SHADER_HOST_CALLABLE: SlangCompileTarget = 16;
pub const SLANG_CUDA_SOURCE: SlangCompileTarget = 17;
pub const SLANG_PTX: SlangCompileTarget = 18;
pub const SLANG_CUDA_OBJECT_CODE: SlangCompileTarget = 19;
pub const SLANG_OBJECT_CODE: SlangCompileTarget = 20;
pub const SLANG_HOST_CPP_SOURCE: SlangCompileTarget = 21;
pub const SLANG_HOST_HOST_CALLABLE: SlangCompileTarget = 22;
pub const SLANG_CPP_PYTORCH_BINDING: SlangCompileTarget = 23;
pub const SLANG_METAL: SlangCompileTarget = 24;
pub const SLANG_METAL_LIB: SlangCompileTarget = 25;
pub const SLANG_METAL_LIB_ASM: SlangCompileTarget = 26;
pub const SLANG_HOST_SHARED_LIBRARY: SlangCompileTarget = 27;
pub const SLANG_WGSL: SlangCompileTarget = 28;
pub const SLANG_WGSL_SPIRV_ASM: SlangCompileTarget = 29;
pub const SLANG_WGSL_SPIRV: SlangCompileTarget = 30;
pub const SLANG_HOST_VM: SlangCompileTarget = 31;
pub const SLANG_CPP_HEADER: SlangCompileTarget = 32;
pub const SLANG_CUDA_HEADER: SlangCompileTarget = 33;
pub const SLANG_HOST_OBJECT_CODE: SlangCompileTarget = 34;
pub const SLANG_HOST_LLVM_IR: SlangCompileTarget = 35;
pub const SLANG_SHADER_LLVM_IR: SlangCompileTarget = 36;
pub const SLANG_TARGET_COUNT_OF: SlangCompileTarget = 37;

pub const SLANG_TARGET_FLAG_PARAMETER_BLOCKS_USE_REGISTER_SPACES: SlangTargetFlags = 1 << 4;
pub const SLANG_TARGET_FLAG_GENERATE_WHOLE_PROGRAM: SlangTargetFlags = 1 << 8;
pub const SLANG_TARGET_FLAG_DUMP_IR: SlangTargetFlags = 1 << 9;
pub const SLANG_TARGET_FLAG_GENERATE_SPIRV_DIRECTLY: SlangTargetFlags = 1 << 10;
pub const K_DEFAULT_TARGET_FLAGS: SlangTargetFlags = SLANG_TARGET_FLAG_GENERATE_SPIRV_DIRECTLY;

pub const SLANG_FLOATING_POINT_MODE_DEFAULT: SlangFloatingPointMode = 0;
pub const SLANG_FLOATING_POINT_MODE_FAST: SlangFloatingPointMode = 1;
pub const SLANG_FLOATING_POINT_MODE_PRECISE: SlangFloatingPointMode = 2;

pub const SLANG_LINE_DIRECTIVE_MODE_DEFAULT: SlangLineDirectiveMode = 0;
pub const SLANG_LINE_DIRECTIVE_MODE_NONE: SlangLineDirectiveMode = 1;
pub const SLANG_LINE_DIRECTIVE_MODE_STANDARD: SlangLineDirectiveMode = 2;
pub const SLANG_LINE_DIRECTIVE_MODE_GLSL: SlangLineDirectiveMode = 3;
pub const SLANG_LINE_DIRECTIVE_MODE_SOURCE_MAP: SlangLineDirectiveMode = 4;

pub const SLANG_MATRIX_LAYOUT_MODE_UNKNOWN: SlangMatrixLayoutMode = 0;
pub const SLANG_MATRIX_LAYOUT_ROW_MAJOR: SlangMatrixLayoutMode = 1;
pub const SLANG_MATRIX_LAYOUT_COLUMN_MAJOR: SlangMatrixLayoutMode = 2;
pub const K_SESSION_FLAGS_NONE: SessionFlags = 0;

pub const SLANG_STAGE_NONE: SlangStage = 0;
pub const SLANG_STAGE_VERTEX: SlangStage = 1;
pub const SLANG_STAGE_HULL: SlangStage = 2;
pub const SLANG_STAGE_DOMAIN: SlangStage = 3;
pub const SLANG_STAGE_GEOMETRY: SlangStage = 4;
pub const SLANG_STAGE_FRAGMENT: SlangStage = 5;
pub const SLANG_STAGE_PIXEL: SlangStage = SLANG_STAGE_FRAGMENT;
pub const SLANG_STAGE_COMPUTE: SlangStage = 6;
pub const SLANG_STAGE_RAY_GENERATION: SlangStage = 7;
pub const SLANG_STAGE_INTERSECTION: SlangStage = 8;
pub const SLANG_STAGE_ANY_HIT: SlangStage = 9;
pub const SLANG_STAGE_CLOSEST_HIT: SlangStage = 10;
pub const SLANG_STAGE_MISS: SlangStage = 11;
pub const SLANG_STAGE_CALLABLE: SlangStage = 12;
pub const SLANG_STAGE_MESH: SlangStage = 13;
pub const SLANG_STAGE_AMPLIFICATION: SlangStage = 14;
pub const SLANG_STAGE_DISPATCH: SlangStage = 15;
pub const SLANG_STAGE_NODE: SlangStage = 16;
pub const SLANG_STAGE_COUNT: SlangStage = 17;

pub type CompilerOptionName = i32;
pub type CompilerOptionValueKind = i32;
pub const COMPILER_OPTION_VALUE_INT: CompilerOptionValueKind = 0;
pub const COMPILER_OPTION_VALUE_STRING: CompilerOptionValueKind = 1;
pub const COMPILER_OPTION_MATRIX_LAYOUT_COLUMN: CompilerOptionName = 8;
pub const COMPILER_OPTION_MATRIX_LAYOUT_ROW: CompilerOptionName = 9;
pub const COMPILER_OPTION_EMIT_SPIRV_DIRECTLY: CompilerOptionName = 58;
pub const COMPILER_OPTION_EMIT_REFLECTION_JSON: CompilerOptionName = 110;

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct CompilerOptionValue {
    pub kind: CompilerOptionValueKind,
    pub int_value0: i32,
    pub int_value1: i32,
    pub string_value0: *const c_char,
    pub string_value1: *const c_char,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct CompilerOptionEntry {
    pub name: CompilerOptionName,
    pub value: CompilerOptionValue,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct SlangGlobalSessionDesc {
    pub structure_size: u32,
    pub api_version: u32,
    pub min_language_version: u32,
    pub enable_glsl: u8,
    pub _enable_glsl_padding: [u8; 3],
    pub reserved: [u32; 16],
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct SlangTargetDesc {
    pub structure_size: usize,
    pub format: SlangCompileTarget,
    pub profile: SlangProfileID,
    pub flags: SlangTargetFlags,
    pub floating_point_mode: SlangFloatingPointMode,
    pub line_directive_mode: SlangLineDirectiveMode,
    pub force_glsl_scalar_buffer_layout: u8,
    pub _force_glsl_scalar_buffer_layout_padding: [u8; 3],
    pub compiler_option_entries: *const CompilerOptionEntry,
    pub compiler_option_entry_count: u32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct SlangPreprocessorMacroDesc {
    pub name: *const c_char,
    pub value: *const c_char,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct SlangSessionDesc {
    pub structure_size: usize,
    pub targets: *const SlangTargetDesc,
    pub target_count: SlangInt,
    pub flags: SessionFlags,
    pub default_matrix_layout_mode: SlangMatrixLayoutMode,
    pub search_paths: *const *const c_char,
    pub search_path_count: SlangInt,
    pub preprocessor_macros: *const SlangPreprocessorMacroDesc,
    pub preprocessor_macro_count: SlangInt,
    pub file_system: *mut ISlangFileSystem,
    pub enable_effect_annotations: u8,
    pub allow_glsl_syntax: u8,
    pub _session_bool_padding: [u8; 6],
    pub compiler_option_entries: *const CompilerOptionEntry,
    pub compiler_option_entry_count: u32,
    pub skip_spirv_validation: u8,
    pub _skip_spirv_validation_padding: [u8; 3],
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct SlangFileSystemDesc {
    pub structure_size: usize,
    pub load_file: Option<SlangLoadFileFunc>,
    pub load_file_user_data: *mut c_void,
}

pub type GlobalSessionDesc = SlangGlobalSessionDesc;
pub type TargetDesc = SlangTargetDesc;
pub type PreprocessorMacroDesc = SlangPreprocessorMacroDesc;
pub type SessionDesc = SlangSessionDesc;
pub type FileSystemDesc = SlangFileSystemDesc;

pub type SlangLoadFileFunc = unsafe extern "C" fn(
    user_data: *mut c_void,
    path: *const c_char,
    out_blob: *mut *mut ISlangBlob,
) -> SlangResult;

#[repr(C)]
pub struct IGlobalSession {
    _private: [u8; 0],
}
pub type GlobalSession = IGlobalSession;

#[repr(C)]
pub struct ISession {
    _private: [u8; 0],
}
pub type Session = ISession;

#[repr(C)]
pub struct IComponentType {
    _private: [u8; 0],
}
pub type ComponentType = IComponentType;
pub type IModule = IComponentType;
pub type IEntryPoint = IComponentType;
pub type Module = IComponentType;
pub type EntryPoint = IComponentType;

#[repr(C)]
pub struct ProgramLayout {
    _private: [u8; 0],
}

#[repr(C)]
pub struct ISlangBlob {
    _private: [u8; 0],
}
pub type IBlob = ISlangBlob;

#[repr(C)]
pub struct ISlangFileSystem {
    _private: [u8; 0],
}
pub type FileSystem = ISlangFileSystem;

unsafe extern "C" {
    pub fn slang_create_global_session(
        desc: *const SlangGlobalSessionDesc,
        out_global_session: *mut *mut IGlobalSession,
    ) -> SlangResult;
    pub fn slang_global_session_destroy(global_session: *mut IGlobalSession);
    pub fn slang_global_session_get_build_tag(
        global_session: *const IGlobalSession,
    ) -> *const c_char;
    pub fn slang_global_session_find_profile(
        global_session: *const IGlobalSession,
        name: *const c_char,
    ) -> SlangProfileID;
    pub fn slang_global_session_check_compile_target_support(
        global_session: *const IGlobalSession,
        target: SlangCompileTarget,
    ) -> SlangResult;
    pub fn slang_global_session_create_session(
        global_session: *const IGlobalSession,
        desc: *const SlangSessionDesc,
        out_session: *mut *mut ISession,
    ) -> SlangResult;

    pub fn slang_file_system_create(
        desc: *const SlangFileSystemDesc,
        out_file_system: *mut *mut ISlangFileSystem,
    ) -> SlangResult;
    pub fn slang_file_system_destroy(file_system: *mut ISlangFileSystem);
    pub fn slang_create_blob(
        data: *const c_void,
        size: usize,
        out_blob: *mut *mut ISlangBlob,
    ) -> SlangResult;

    pub fn slang_session_destroy(session: *mut ISession);
    pub fn slang_session_load_module_from_source(
        session: *mut ISession,
        module_name: *const c_char,
        path: *const c_char,
        source: *mut ISlangBlob,
        out_diagnostics: *mut *mut ISlangBlob,
        out_module: *mut *mut IModule,
    ) -> SlangResult;
    pub fn slang_session_create_composite_component_type(
        session: *mut ISession,
        component_types: *const *mut IComponentType,
        component_type_count: SlangInt,
        out_component_type: *mut *mut IComponentType,
        out_diagnostics: *mut *mut ISlangBlob,
    ) -> SlangResult;

    pub fn slang_module_find_and_check_entry_point(
        module: *mut IModule,
        name: *const c_char,
        stage: SlangStage,
        out_entry_point: *mut *mut IEntryPoint,
        out_diagnostics: *mut *mut ISlangBlob,
    ) -> SlangResult;
    pub fn slang_module_get_name(module: *const IModule) -> *const c_char;
    pub fn slang_module_get_file_path(module: *const IModule) -> *const c_char;

    pub fn slang_component_type_destroy(component_type: *mut IComponentType);
    pub fn slang_component_type_link(
        component_type: *mut IComponentType,
        out_linked_component_type: *mut *mut IComponentType,
        out_diagnostics: *mut *mut ISlangBlob,
    ) -> SlangResult;
    pub fn slang_component_type_get_target_code(
        component_type: *mut IComponentType,
        target_index: SlangInt,
        out_code: *mut *mut ISlangBlob,
        out_diagnostics: *mut *mut ISlangBlob,
    ) -> SlangResult;
    pub fn slang_component_type_get_entry_point_code(
        component_type: *mut IComponentType,
        entry_point_index: SlangInt,
        target_index: SlangInt,
        out_code: *mut *mut ISlangBlob,
        out_diagnostics: *mut *mut ISlangBlob,
    ) -> SlangResult;
    pub fn slang_component_type_get_layout(
        component_type: *mut IComponentType,
        target_index: SlangInt,
        out_layout: *mut *mut ProgramLayout,
        out_diagnostics: *mut *mut ISlangBlob,
    ) -> SlangResult;

    pub fn slang_program_layout_destroy(layout: *mut ProgramLayout);
    pub fn slang_program_layout_to_json(
        layout: *mut ProgramLayout,
        out_json: *mut *mut ISlangBlob,
    ) -> SlangResult;

    pub fn slang_blob_destroy(blob: *mut ISlangBlob);
    pub fn slang_blob_get_buffer_pointer(blob: *mut ISlangBlob) -> *const c_void;
    pub fn slang_blob_get_buffer_size(blob: *mut ISlangBlob) -> usize;

    pub fn slang_abi_version() -> u32;
}

/// Namespace-style aliases for callers that want to mirror Slang's object model
/// while remaining in the raw FFI crate. These are symbol aliases only; they
/// do not add ownership or safety policy.
pub mod slang {
    pub use super::{
        COMPILER_OPTION_EMIT_REFLECTION_JSON, COMPILER_OPTION_EMIT_SPIRV_DIRECTLY,
        COMPILER_OPTION_MATRIX_LAYOUT_COLUMN, COMPILER_OPTION_MATRIX_LAYOUT_ROW,
        COMPILER_OPTION_VALUE_INT, COMPILER_OPTION_VALUE_STRING, K_DEFAULT_TARGET_FLAGS,
        K_SESSION_FLAGS_NONE, SLANG_API_VERSION, SLANG_C_SOURCE, SLANG_CPP_HEADER,
        SLANG_CPP_PYTORCH_BINDING, SLANG_CPP_SOURCE, SLANG_CUDA_HEADER, SLANG_CUDA_OBJECT_CODE,
        SLANG_CUDA_SOURCE, SLANG_DXBC, SLANG_DXBC_ASM, SLANG_DXIL, SLANG_DXIL_ASM,
        SLANG_E_CANNOT_OPEN, SLANG_E_INVALID_ARG, SLANG_E_INVALID_HANDLE, SLANG_E_NO_INTERFACE,
        SLANG_E_NOT_AVAILABLE, SLANG_E_NOT_FOUND, SLANG_E_NOT_IMPLEMENTED, SLANG_E_OUT_OF_MEMORY,
        SLANG_FAIL, SLANG_FLOATING_POINT_MODE_DEFAULT, SLANG_FLOATING_POINT_MODE_FAST,
        SLANG_FLOATING_POINT_MODE_PRECISE, SLANG_GLSL, SLANG_GLSL_VULKAN_DEPRECATED,
        SLANG_GLSL_VULKAN_ONE_DESC_DEPRECATED, SLANG_HLSL, SLANG_HOST_CPP_SOURCE,
        SLANG_HOST_EXECUTABLE, SLANG_HOST_HOST_CALLABLE, SLANG_HOST_LLVM_IR,
        SLANG_HOST_OBJECT_CODE, SLANG_HOST_SHARED_LIBRARY, SLANG_HOST_VM,
        SLANG_LANGUAGE_VERSION_202A, SLANG_LANGUAGE_VERSION_202B, SLANG_LANGUAGE_VERSION_202C,
        SLANG_LANGUAGE_VERSION_2025, SLANG_LANGUAGE_VERSION_2026, SLANG_LANGUAGE_VERSION_DEFAULT,
        SLANG_LANGUAGE_VERSION_LATEST, SLANG_LANGUAGE_VERSION_LEGACY, SLANG_LANGUAGE_VERSION_NEXT,
        SLANG_LANGUAGE_VERSION_UNKNOWN, SLANG_LINE_DIRECTIVE_MODE_DEFAULT,
        SLANG_LINE_DIRECTIVE_MODE_GLSL, SLANG_LINE_DIRECTIVE_MODE_NONE,
        SLANG_LINE_DIRECTIVE_MODE_SOURCE_MAP, SLANG_LINE_DIRECTIVE_MODE_STANDARD,
        SLANG_MATRIX_LAYOUT_COLUMN_MAJOR, SLANG_MATRIX_LAYOUT_MODE_UNKNOWN,
        SLANG_MATRIX_LAYOUT_ROW_MAJOR, SLANG_METAL, SLANG_METAL_LIB, SLANG_METAL_LIB_ASM,
        SLANG_OBJECT_CODE, SLANG_OK, SLANG_PROFILE_UNKNOWN, SLANG_PTX, SLANG_SHADER_HOST_CALLABLE,
        SLANG_SHADER_LLVM_IR, SLANG_SHADER_SHARED_LIBRARY, SLANG_SPIRV, SLANG_SPIRV_ASM,
        SLANG_STAGE_AMPLIFICATION, SLANG_STAGE_ANY_HIT, SLANG_STAGE_CALLABLE,
        SLANG_STAGE_CLOSEST_HIT, SLANG_STAGE_COMPUTE, SLANG_STAGE_COUNT, SLANG_STAGE_DISPATCH,
        SLANG_STAGE_DOMAIN, SLANG_STAGE_FRAGMENT, SLANG_STAGE_GEOMETRY, SLANG_STAGE_HULL,
        SLANG_STAGE_INTERSECTION, SLANG_STAGE_MESH, SLANG_STAGE_MISS, SLANG_STAGE_NODE,
        SLANG_STAGE_NONE, SLANG_STAGE_PIXEL, SLANG_STAGE_RAY_GENERATION, SLANG_STAGE_VERTEX,
        SLANG_TARGET_COUNT_OF, SLANG_TARGET_FLAG_DUMP_IR,
        SLANG_TARGET_FLAG_GENERATE_SPIRV_DIRECTLY, SLANG_TARGET_FLAG_GENERATE_WHOLE_PROGRAM,
        SLANG_TARGET_FLAG_PARAMETER_BLOCKS_USE_REGISTER_SPACES, SLANG_TARGET_NONE,
        SLANG_TARGET_UNKNOWN, SLANG_WGSL, SLANG_WGSL_SPIRV, SLANG_WGSL_SPIRV_ASM,
    };
    pub use super::{
        CompilerOptionEntry, CompilerOptionName, CompilerOptionValue, CompilerOptionValueKind,
        EntryPoint, FileSystem, FileSystemDesc, GlobalSession, GlobalSessionDesc, IBlob,
        IComponentType, IEntryPoint, IGlobalSession, IModule, ISession, ISlangBlob,
        ISlangFileSystem, Module, PreprocessorMacroDesc, ProgramLayout, Session, SessionDesc,
        SessionFlags, SlangCompileTarget, SlangCompileTargetIntegral, SlangFileSystemDesc,
        SlangFloatingPointMode, SlangFloatingPointModeIntegral, SlangGlobalSessionDesc, SlangInt,
        SlangInt32, SlangLineDirectiveMode, SlangLineDirectiveModeIntegral, SlangLoadFileFunc,
        SlangMatrixLayoutMode, SlangMatrixLayoutModeIntegral, SlangPreprocessorMacroDesc,
        SlangProfileID, SlangProfileIDIntegral, SlangResult, SlangSessionDesc, SlangStage,
        SlangStageIntegral, SlangTargetDesc, SlangTargetFlags, SlangUInt, SlangUInt32, TargetDesc,
    };
    pub use super::{
        slang_abi_version as abi_version, slang_blob_destroy as blob_destroy,
        slang_blob_get_buffer_pointer as blob_data, slang_blob_get_buffer_size as blob_size,
        slang_component_type_destroy as component_type_destroy,
        slang_component_type_destroy as component_type_release,
        slang_component_type_get_entry_point_code as component_type_get_entry_point_code,
        slang_component_type_get_layout as component_type_get_layout,
        slang_component_type_get_target_code as component_type_get_target_code,
        slang_component_type_link as component_type_link, slang_create_blob as create_blob,
        slang_create_global_session as create_global_session2,
        slang_file_system_create as create_file_system_adapter,
        slang_file_system_destroy as destroy_file_system_adapter,
        slang_global_session_check_compile_target_support as check_compile_target_support,
        slang_global_session_create_session as global_session_create_session,
        slang_global_session_destroy as destroy_global_session,
        slang_global_session_find_profile as find_profile,
        slang_global_session_get_build_tag as get_build_tag,
        slang_module_find_and_check_entry_point as module_find_and_check_entry_point,
        slang_module_get_file_path as module_get_file_path,
        slang_module_get_name as module_get_name,
        slang_program_layout_destroy as destroy_program_layout,
        slang_program_layout_to_json as program_layout_to_json,
        slang_session_create_composite_component_type as session_create_composite_component_type,
        slang_session_destroy as destroy_session,
        slang_session_load_module_from_source as load_module_from_source,
    };
}
