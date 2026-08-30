//! Raw FFI declarations for the project-owned `slang-slim` native ABI.
//!
//! The declarations intentionally do not expose Slang's C++ interfaces. The
//! native artifact selection and linker directives remain in `build.rs`, so the
//! crate can be checked before a GitHub Release asset is available locally.

#![no_std]

use core::ffi::{c_char, c_void};

pub const ABI_VERSION: u32 = 1;

pub type Status = i32;
pub const STATUS_OK: Status = 0;
pub const STATUS_INVALID_ARGUMENT: Status = -1;
pub const STATUS_OUT_OF_MEMORY: Status = -2;
pub const STATUS_COMPILE_ERROR: Status = -3;
pub const STATUS_UNSUPPORTED: Status = -4;
pub const STATUS_NOT_FOUND: Status = -5;
pub const STATUS_IO_ERROR: Status = -6;
pub const STATUS_INTERNAL_ERROR: Status = -7;

pub type Target = u32;
pub const TARGET_HLSL: Target = 1;
pub const TARGET_SPIRV: Target = 2;
pub const TARGET_METAL: Target = 3;

/// Values mirror SlangCompileTarget. The legacy `Target` aliases above remain
/// available for callers that only need the original three output kinds.
pub type CompileTarget = u32;
pub const COMPILE_TARGET_UNKNOWN: CompileTarget = 0;
pub const COMPILE_TARGET_NONE: CompileTarget = 1;
pub const COMPILE_TARGET_GLSL: CompileTarget = 2;
pub const COMPILE_TARGET_GLSL_VULKAN_DEPRECATED: CompileTarget = 3;
pub const COMPILE_TARGET_GLSL_VULKAN_ONE_DESC_DEPRECATED: CompileTarget = 4;
pub const COMPILE_TARGET_HLSL: CompileTarget = 5;
pub const COMPILE_TARGET_SPIRV: CompileTarget = 6;
pub const COMPILE_TARGET_SPIRV_ASM: CompileTarget = 7;
pub const COMPILE_TARGET_DXBC: CompileTarget = 8;
pub const COMPILE_TARGET_DXBC_ASM: CompileTarget = 9;
pub const COMPILE_TARGET_DXIL: CompileTarget = 10;
pub const COMPILE_TARGET_DXIL_ASM: CompileTarget = 11;
pub const COMPILE_TARGET_C_SOURCE: CompileTarget = 12;
pub const COMPILE_TARGET_CPP_SOURCE: CompileTarget = 13;
pub const COMPILE_TARGET_HOST_EXECUTABLE: CompileTarget = 14;
pub const COMPILE_TARGET_SHADER_SHARED_LIBRARY: CompileTarget = 15;
pub const COMPILE_TARGET_SHADER_HOST_CALLABLE: CompileTarget = 16;
pub const COMPILE_TARGET_CUDA_SOURCE: CompileTarget = 17;
pub const COMPILE_TARGET_PTX: CompileTarget = 18;
pub const COMPILE_TARGET_CUDA_OBJECT_CODE: CompileTarget = 19;
pub const COMPILE_TARGET_OBJECT_CODE: CompileTarget = 20;
pub const COMPILE_TARGET_HOST_CPP_SOURCE: CompileTarget = 21;
pub const COMPILE_TARGET_HOST_HOST_CALLABLE: CompileTarget = 22;
pub const COMPILE_TARGET_CPP_PYTORCH_BINDING: CompileTarget = 23;
pub const COMPILE_TARGET_METAL: CompileTarget = 24;
pub const COMPILE_TARGET_METAL_LIB: CompileTarget = 25;
pub const COMPILE_TARGET_METAL_LIB_ASM: CompileTarget = 26;
pub const COMPILE_TARGET_HOST_SHARED_LIBRARY: CompileTarget = 27;
pub const COMPILE_TARGET_WGSL: CompileTarget = 28;
pub const COMPILE_TARGET_WGSL_SPIRV_ASM: CompileTarget = 29;
pub const COMPILE_TARGET_WGSL_SPIRV: CompileTarget = 30;
pub const COMPILE_TARGET_HOST_VM: CompileTarget = 31;
pub const COMPILE_TARGET_CPP_HEADER: CompileTarget = 32;
pub const COMPILE_TARGET_CUDA_HEADER: CompileTarget = 33;
pub const COMPILE_TARGET_HOST_OBJECT_CODE: CompileTarget = 34;
pub const COMPILE_TARGET_HOST_LLVM_IR: CompileTarget = 35;
pub const COMPILE_TARGET_SHADER_LLVM_IR: CompileTarget = 36;
pub const COMPILE_TARGET_COUNT_OF: CompileTarget = 37;

pub type Stage = u32;
pub const STAGE_NONE: Stage = 0;
pub const STAGE_VERTEX: Stage = 1;
pub const STAGE_HULL: Stage = 2;
pub const STAGE_DOMAIN: Stage = 3;
pub const STAGE_GEOMETRY: Stage = 4;
pub const STAGE_FRAGMENT: Stage = 5;
pub const STAGE_PIXEL: Stage = STAGE_FRAGMENT;
pub const STAGE_COMPUTE: Stage = 6;
pub const STAGE_RAY_GENERATION: Stage = 7;
pub const STAGE_INTERSECTION: Stage = 8;
pub const STAGE_ANY_HIT: Stage = 9;
pub const STAGE_CLOSEST_HIT: Stage = 10;
pub const STAGE_MISS: Stage = 11;
pub const STAGE_CALLABLE: Stage = 12;
pub const STAGE_MESH: Stage = 13;
pub const STAGE_AMPLIFICATION: Stage = 14;
pub const STAGE_DISPATCH: Stage = 15;
pub const STAGE_NODE: Stage = 16;
pub const STAGE_COUNT_OF: Stage = 17;
pub const STAGE_FRAGMENT_LEGACY: Stage = 2;
pub const STAGE_COMPUTE_LEGACY: Stage = 3;

pub type TargetFlags = u32;
pub const TARGET_FLAG_PARAMETER_BLOCKS_USE_REGISTER_SPACES: TargetFlags = 1 << 4;
pub const TARGET_FLAG_GENERATE_WHOLE_PROGRAM: TargetFlags = 1 << 8;
pub const TARGET_FLAG_DUMP_IR: TargetFlags = 1 << 9;
pub const TARGET_FLAG_GENERATE_SPIRV_DIRECTLY: TargetFlags = 1 << 10;
pub const TARGET_FLAGS_DEFAULT: TargetFlags = TARGET_FLAG_GENERATE_SPIRV_DIRECTLY;

pub type FloatingPointMode = u32;
pub const FLOATING_POINT_MODE_DEFAULT: FloatingPointMode = 0;
pub const FLOATING_POINT_MODE_FAST: FloatingPointMode = 1;
pub const FLOATING_POINT_MODE_PRECISE: FloatingPointMode = 2;

pub type LineDirectiveMode = u32;
pub const LINE_DIRECTIVE_MODE_DEFAULT: LineDirectiveMode = 0;
pub const LINE_DIRECTIVE_MODE_NONE: LineDirectiveMode = 1;
pub const LINE_DIRECTIVE_MODE_STANDARD: LineDirectiveMode = 2;
pub const LINE_DIRECTIVE_MODE_GLSL: LineDirectiveMode = 3;
pub const LINE_DIRECTIVE_MODE_SOURCE_MAP: LineDirectiveMode = 4;

pub type MatrixLayoutMode = u32;
pub const MATRIX_LAYOUT_MODE_UNKNOWN: MatrixLayoutMode = 0;
pub const MATRIX_LAYOUT_ROW_MAJOR: MatrixLayoutMode = 1;
pub const MATRIX_LAYOUT_COLUMN_MAJOR: MatrixLayoutMode = 2;

pub type SessionFlags = u32;
pub const SESSION_FLAGS_NONE: SessionFlags = 0;

/// Values mirror slang::CompilerOptionValueKind and CompilerOptionValue.
pub type CompilerOptionValueKind = u32;
pub const COMPILER_OPTION_VALUE_INT: CompilerOptionValueKind = 0;
pub const COMPILER_OPTION_VALUE_STRING: CompilerOptionValueKind = 1;

/// Common CompilerOptionName values. The entry type also accepts numeric
/// values added by newer Slang releases.
pub type CompilerOptionName = u32;
pub const COMPILER_OPTION_MATRIX_LAYOUT_COLUMN: CompilerOptionName = 8;
pub const COMPILER_OPTION_MATRIX_LAYOUT_ROW: CompilerOptionName = 9;
pub const COMPILER_OPTION_EMIT_SPIRV_DIRECTLY: CompilerOptionName = 58;
pub const COMPILER_OPTION_EMIT_REFLECTION_JSON: CompilerOptionName = 110;

#[repr(C)]
pub struct Compiler {
    _private: [u8; 0],
}

#[repr(C)]
pub struct Compilation {
    _private: [u8; 0],
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct Blob {
    pub data: *const u8,
    pub size: usize,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct TargetDesc {
    pub struct_size: u32,
    pub target: Target,
    pub format: CompileTarget,
    pub profile: *const c_char,
    pub flags: TargetFlags,
    pub floating_point_mode: FloatingPointMode,
    pub line_directive_mode: LineDirectiveMode,
    pub force_glsl_scalar_buffer_layout: u32,
    pub compiler_options: *const CompilerOptionEntry,
    pub compiler_option_count: usize,
}

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
    /// Numeric value mirrors slang::CompilerOptionName.
    pub name: CompilerOptionName,
    pub value: CompilerOptionValue,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct EntryPointDesc {
    pub struct_size: u32,
    pub name: *const c_char,
    pub stage: Stage,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct DefineDesc {
    pub struct_size: u32,
    pub name: *const c_char,
    pub value: *const c_char,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct VirtualFile {
    pub struct_size: u32,
    pub path: *const c_char,
    pub data: *const u8,
    pub size: usize,
}

pub type LoadFileFn = unsafe extern "C" fn(
    user_data: *mut c_void,
    normalized_path: *const c_char,
    out_file: *mut Blob,
) -> Status;

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct CompileDesc {
    pub struct_size: u32,
    pub module_name: *const c_char,
    pub source_path: *const c_char,
    pub source: *const u8,
    pub source_size: usize,
    pub entry_points: *const EntryPointDesc,
    pub entry_point_count: usize,
    pub targets: *const TargetDesc,
    pub target_count: usize,
    pub defines: *const DefineDesc,
    pub define_count: usize,
    pub virtual_files: *const VirtualFile,
    pub virtual_file_count: usize,
    pub load_file: Option<LoadFileFn>,
    pub load_file_user_data: *mut c_void,
    pub search_paths: *const *const c_char,
    pub search_path_count: usize,
    pub session_flags: u32,
    pub default_matrix_layout_mode: MatrixLayoutMode,
    pub allow_glsl_syntax: u32,
    pub skip_spirv_validation: u32,
    pub enable_effect_annotations: u32,
    pub compiler_options: *const CompilerOptionEntry,
    pub compiler_option_count: usize,
}

unsafe extern "C" {
    pub fn slang_slim_abi_version() -> u32;

    pub fn slang_slim_compiler_create(out_compiler: *mut *mut Compiler) -> Status;
    pub fn slang_slim_compiler_destroy(compiler: *mut Compiler);
    pub fn slang_slim_compiler_build_tag(compiler: *const Compiler) -> *const c_char;
    pub fn slang_slim_compiler_supports_target(compiler: *const Compiler, target: Target) -> i32;
    pub fn slang_slim_compiler_supports_target_format(
        compiler: *const Compiler,
        format: CompileTarget,
        profile: *const c_char,
    ) -> i32;

    pub fn slang_slim_compile(
        compiler: *const Compiler,
        desc: *const CompileDesc,
        out_compilation: *mut *mut Compilation,
    ) -> Status;
    pub fn slang_slim_compilation_destroy(compilation: *mut Compilation);
    pub fn slang_slim_compilation_target_count(compilation: *const Compilation) -> usize;
    pub fn slang_slim_compilation_entry_point_count(compilation: *const Compilation) -> usize;
    pub fn slang_slim_compilation_target(
        compilation: *const Compilation,
        target_index: usize,
    ) -> Target;
    pub fn slang_slim_compilation_target_format(
        compilation: *const Compilation,
        target_index: usize,
    ) -> CompileTarget;
    pub fn slang_slim_compilation_target_profile(
        compilation: *const Compilation,
        target_index: usize,
    ) -> *const c_char;
    pub fn slang_slim_compilation_entry_point_name(
        compilation: *const Compilation,
        entry_point_index: usize,
    ) -> *const c_char;
    pub fn slang_slim_compilation_get_code(
        compilation: *const Compilation,
        target_index: usize,
        entry_point_index: usize,
        out_code: *mut Blob,
    ) -> Status;
    pub fn slang_slim_compilation_get_reflection_json(
        compilation: *const Compilation,
        target_index: usize,
        out_json: *mut Blob,
    ) -> Status;
    pub fn slang_slim_compilation_get_diagnostics(
        compilation: *const Compilation,
        out_diagnostics: *mut Blob,
    ) -> Status;
}
