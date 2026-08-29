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

pub type Stage = u32;
pub const STAGE_VERTEX: Stage = 1;
pub const STAGE_FRAGMENT: Stage = 2;
pub const STAGE_COMPUTE: Stage = 3;

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
}

unsafe extern "C" {
    pub fn slang_slim_abi_version() -> u32;

    pub fn slang_slim_compiler_create(out_compiler: *mut *mut Compiler) -> Status;
    pub fn slang_slim_compiler_destroy(compiler: *mut Compiler);
    pub fn slang_slim_compiler_build_tag(compiler: *const Compiler) -> *const c_char;
    pub fn slang_slim_compiler_supports_target(compiler: *const Compiler, target: Target) -> i32;

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
