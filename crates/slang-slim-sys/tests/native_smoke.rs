#![cfg(slang_slim_native_linked)]

use core::mem::size_of;
use core::ptr;
use std::ffi::{CStr, CString, c_void};
use std::sync::atomic::{AtomicUsize, Ordering};

use slang_slim_sys::{
    ABI_VERSION, Blob, COMPILE_TARGET_HLSL, COMPILE_TARGET_METAL, COMPILE_TARGET_SPIRV,
    COMPILER_OPTION_MATRIX_LAYOUT_ROW, COMPILER_OPTION_VALUE_INT, Compilation, CompileDesc,
    Compiler, CompilerOptionEntry, CompilerOptionValue, EntryPointDesc, LoadFileFn, STAGE_COMPUTE,
    STAGE_COMPUTE_LEGACY, STAGE_FRAGMENT, STAGE_FRAGMENT_LEGACY, STAGE_VERTEX,
    STATUS_INVALID_ARGUMENT, STATUS_NOT_FOUND, STATUS_OK, Status, TARGET_FLAGS_DEFAULT,
    TARGET_HLSL, TARGET_METAL, TARGET_SPIRV, Target, TargetDesc, VirtualFile,
    slang_slim_abi_version, slang_slim_compilation_destroy,
    slang_slim_compilation_entry_point_count, slang_slim_compilation_get_code,
    slang_slim_compilation_get_diagnostics, slang_slim_compilation_get_reflection_json,
    slang_slim_compilation_target, slang_slim_compilation_target_count,
    slang_slim_compilation_target_format, slang_slim_compilation_target_profile,
    slang_slim_compile, slang_slim_compiler_create, slang_slim_compiler_destroy,
    slang_slim_compiler_supports_target, slang_slim_compiler_supports_target_format,
};

static SHARED_SOURCE: &[u8] = b"float4 shared_tint() { return float4(1.0, 0.75, 0.5, 1.0); }\n";

static SOURCE: &[u8] = br#"
#include "shared.hlsl"

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VertexOutput vertex_main(uint vertexId : SV_VertexID)
{
    VertexOutput output;
    output.position = float4(vertexId == 0 ? -1.0 : 1.0, 0.0, 0.0, 1.0);
    output.uv = float2(0.0, 0.0);
    return output;
}

[shader("fragment")]
float4 fragment_main(VertexOutput input) : SV_Target0
{
    return shared_tint() + float4(input.uv, 0.0, 0.0);
}

[shader("compute")]
[numthreads(1, 1, 1)]
void compute_main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
}
"#;

#[cfg(target_os = "android")]
const EXPECTED_TARGETS: &[Target] = &[TARGET_SPIRV];

#[cfg(not(target_os = "android"))]
const EXPECTED_TARGETS: &[Target] = &[TARGET_HLSL, TARGET_SPIRV, TARGET_METAL];

fn empty_blob() -> Blob {
    Blob {
        data: ptr::null(),
        size: 0,
    }
}

fn blob_contains(blob: Blob, needle: &[u8]) -> bool {
    if blob.data.is_null() || blob.size < needle.len() {
        return false;
    }
    // The native ABI owns this view for the lifetime of the compilation handle.
    let bytes = unsafe { core::slice::from_raw_parts(blob.data, blob.size) };
    bytes.windows(needle.len()).any(|window| window == needle)
}

fn blob_u32(blob: Blob, index: usize) -> Option<u32> {
    let byte_offset = index.checked_mul(size_of::<u32>())?;
    let end = byte_offset.checked_add(size_of::<u32>())?;
    if blob.data.is_null() || blob.size < end {
        return None;
    }
    let bytes = unsafe { core::slice::from_raw_parts(blob.data, blob.size) };
    Some(u32::from_le_bytes(bytes[byte_offset..end].try_into().ok()?))
}

fn blob_text(blob: Blob) -> String {
    if blob.data.is_null() || blob.size == 0 {
        return String::new();
    }
    let bytes = unsafe { core::slice::from_raw_parts(blob.data, blob.size) };
    String::from_utf8_lossy(bytes).into_owned()
}

fn expected_format(target: Target) -> u32 {
    match target {
        TARGET_HLSL => COMPILE_TARGET_HLSL,
        TARGET_SPIRV => COMPILE_TARGET_SPIRV,
        TARGET_METAL => COMPILE_TARGET_METAL,
        _ => 0,
    }
}

#[repr(C)]
#[derive(Clone, Copy)]
struct LegacyTargetDesc {
    struct_size: u32,
    target: Target,
}

#[repr(C)]
struct LegacyCompileDesc {
    struct_size: u32,
    module_name: *const std::ffi::c_char,
    source_path: *const std::ffi::c_char,
    source: *const u8,
    source_size: usize,
    entry_points: *const EntryPointDesc,
    entry_point_count: usize,
    targets: *const LegacyTargetDesc,
    target_count: usize,
    defines: *const slang_slim_sys::DefineDesc,
    define_count: usize,
    virtual_files: *const VirtualFile,
    virtual_file_count: usize,
    load_file: Option<LoadFileFn>,
    load_file_user_data: *mut c_void,
}

fn entry_points(names: &[CString; 3]) -> [EntryPointDesc; 3] {
    [
        EntryPointDesc {
            struct_size: size_of::<EntryPointDesc>() as u32,
            name: names[0].as_ptr(),
            stage: STAGE_VERTEX,
        },
        EntryPointDesc {
            struct_size: size_of::<EntryPointDesc>() as u32,
            name: names[1].as_ptr(),
            stage: STAGE_FRAGMENT,
        },
        EntryPointDesc {
            struct_size: size_of::<EntryPointDesc>() as u32,
            name: names[2].as_ptr(),
            stage: STAGE_COMPUTE,
        },
    ]
}

fn target_descriptors() -> Vec<TargetDesc> {
    EXPECTED_TARGETS
        .iter()
        .copied()
        .map(|target| TargetDesc {
            struct_size: size_of::<TargetDesc>() as u32,
            target,
            format: expected_format(target),
            profile: ptr::null(),
            flags: TARGET_FLAGS_DEFAULT,
            floating_point_mode: 0,
            line_directive_mode: 0,
            force_glsl_scalar_buffer_layout: 0,
            compiler_options: ptr::null(),
            compiler_option_count: 0,
        })
        .collect()
}

unsafe fn compile_fixture(
    compiler: *const Compiler,
    use_virtual_file: bool,
    load_file: Option<LoadFileFn>,
    load_file_user_data: *mut c_void,
    module_name_value: &str,
) -> (Status, *mut Compilation) {
    let module_name = CString::new(module_name_value).expect("module name has no NUL");
    let source_path = CString::new("main.hlsl").expect("literal has no NUL");
    let entry_names = [
        CString::new("vertex_main").expect("literal has no NUL"),
        CString::new("fragment_main").expect("literal has no NUL"),
        CString::new("compute_main").expect("literal has no NUL"),
    ];
    let entries = entry_points(&entry_names);
    let mut targets = target_descriptors();
    let compiler_options = [CompilerOptionEntry {
        name: COMPILER_OPTION_MATRIX_LAYOUT_ROW,
        value: CompilerOptionValue {
            kind: COMPILER_OPTION_VALUE_INT,
            int_value0: 1,
            int_value1: 0,
            string_value0: ptr::null(),
            string_value1: ptr::null(),
        },
    }];
    for target in &mut targets {
        target.compiler_options = compiler_options.as_ptr();
        target.compiler_option_count = compiler_options.len();
    }
    let virtual_file_path = CString::new("shared.hlsl").expect("literal has no NUL");
    let virtual_file = VirtualFile {
        struct_size: size_of::<VirtualFile>() as u32,
        path: virtual_file_path.as_ptr(),
        data: SHARED_SOURCE.as_ptr(),
        size: SHARED_SOURCE.len(),
    };

    let (virtual_files, virtual_file_count) = if use_virtual_file {
        (&virtual_file as *const VirtualFile, 1)
    } else {
        (ptr::null(), 0)
    };
    let desc = CompileDesc {
        struct_size: size_of::<CompileDesc>() as u32,
        module_name: module_name.as_ptr(),
        source_path: source_path.as_ptr(),
        source: SOURCE.as_ptr(),
        source_size: SOURCE.len(),
        entry_points: entries.as_ptr(),
        entry_point_count: entries.len(),
        targets: targets.as_ptr(),
        target_count: targets.len(),
        defines: ptr::null(),
        define_count: 0,
        virtual_files,
        virtual_file_count,
        load_file,
        load_file_user_data,
        search_paths: ptr::null(),
        search_path_count: 0,
        session_flags: 0,
        default_matrix_layout_mode: 0,
        allow_glsl_syntax: 0,
        skip_spirv_validation: 0,
        enable_effect_annotations: 0,
        compiler_options: compiler_options.as_ptr(),
        compiler_option_count: compiler_options.len(),
    };

    let mut compilation = ptr::null_mut();
    let status = unsafe { slang_slim_compile(compiler, &desc, &mut compilation) };
    (status, compilation)
}

unsafe fn compile_legacy_fixture(compiler: *const Compiler) -> (Status, *mut Compilation) {
    let module_name = CString::new("rust_ffi_legacy_fixture").expect("literal has no NUL");
    let source_path = CString::new("legacy.hlsl").expect("literal has no NUL");
    let entry_names = [
        CString::new("vertex_main").expect("literal has no NUL"),
        CString::new("fragment_main").expect("literal has no NUL"),
        CString::new("compute_main").expect("literal has no NUL"),
    ];
    let entries = [
        EntryPointDesc {
            struct_size: size_of::<EntryPointDesc>() as u32,
            name: entry_names[0].as_ptr(),
            stage: STAGE_VERTEX,
        },
        EntryPointDesc {
            struct_size: size_of::<EntryPointDesc>() as u32,
            name: entry_names[1].as_ptr(),
            stage: STAGE_FRAGMENT_LEGACY,
        },
        EntryPointDesc {
            struct_size: size_of::<EntryPointDesc>() as u32,
            name: entry_names[2].as_ptr(),
            stage: STAGE_COMPUTE_LEGACY,
        },
    ];
    let target = LegacyTargetDesc {
        struct_size: size_of::<LegacyTargetDesc>() as u32,
        target: TARGET_SPIRV,
    };
    let virtual_file_path = CString::new("shared.hlsl").expect("literal has no NUL");
    let virtual_file = VirtualFile {
        struct_size: size_of::<VirtualFile>() as u32,
        path: virtual_file_path.as_ptr(),
        data: SHARED_SOURCE.as_ptr(),
        size: SHARED_SOURCE.len(),
    };
    let desc = LegacyCompileDesc {
        struct_size: size_of::<LegacyCompileDesc>() as u32,
        module_name: module_name.as_ptr(),
        source_path: source_path.as_ptr(),
        source: SOURCE.as_ptr(),
        source_size: SOURCE.len(),
        entry_points: entries.as_ptr(),
        entry_point_count: entries.len(),
        targets: &target,
        target_count: 1,
        defines: ptr::null(),
        define_count: 0,
        virtual_files: &virtual_file,
        virtual_file_count: 1,
        load_file: None,
        load_file_user_data: ptr::null_mut(),
    };

    let mut compilation = ptr::null_mut();
    let status = unsafe {
        slang_slim_compile(
            compiler,
            (&desc as *const LegacyCompileDesc).cast::<CompileDesc>(),
            &mut compilation,
        )
    };
    (status, compilation)
}

unsafe fn assert_compile_success(status: Status, compilation: *mut Compilation) {
    if status == STATUS_OK {
        assert!(!compilation.is_null());
        return;
    }

    let mut diagnostics = empty_blob();
    let diagnostic_text;
    if !compilation.is_null() {
        unsafe {
            let _ = slang_slim_compilation_get_diagnostics(compilation, &mut diagnostics);
        }
        diagnostic_text = blob_text(diagnostics);
        unsafe { slang_slim_compilation_destroy(compilation) };
    } else {
        diagnostic_text = String::new();
    }
    panic!("native compile failed with status {status}: {diagnostic_text}");
}

unsafe fn assert_outputs(compilation: *const Compilation, expected_targets: &[Target]) {
    assert_eq!(
        unsafe { slang_slim_compilation_target_count(compilation) },
        expected_targets.len()
    );
    assert_eq!(
        unsafe { slang_slim_compilation_entry_point_count(compilation) },
        3
    );

    for (target_index, expected_target) in expected_targets.iter().enumerate() {
        assert_eq!(
            unsafe { slang_slim_compilation_target(compilation, target_index) },
            *expected_target
        );
        assert_eq!(
            unsafe { slang_slim_compilation_target_format(compilation, target_index) },
            expected_format(*expected_target)
        );
        let profile = unsafe { slang_slim_compilation_target_profile(compilation, target_index) };
        assert!(!profile.is_null());
        let profile = unsafe { CStr::from_ptr(profile) }.to_bytes();
        let expected_profile = match *expected_target {
            TARGET_HLSL => b"sm_6_0".as_slice(),
            TARGET_SPIRV => b"spirv_1_3".as_slice(),
            TARGET_METAL => b"metallib_2_3".as_slice(),
            _ => &[],
        };
        assert_eq!(profile, expected_profile);

        let mut reflection = empty_blob();
        assert_eq!(
            unsafe {
                slang_slim_compilation_get_reflection_json(
                    compilation,
                    target_index,
                    &mut reflection,
                )
            },
            STATUS_OK
        );
        for name in [b"vertex_main".as_slice(), b"fragment_main", b"compute_main"] {
            assert!(blob_contains(reflection, name));
        }

        for entry_index in 0..3 {
            let mut code = empty_blob();
            assert_eq!(
                unsafe {
                    slang_slim_compilation_get_code(
                        compilation,
                        target_index,
                        entry_index,
                        &mut code,
                    )
                },
                STATUS_OK
            );
            assert!(!code.data.is_null());
            assert!(code.size > 0);
            if *expected_target == TARGET_SPIRV {
                assert_eq!(blob_u32(code, 0), Some(0x0723_0203));
                assert_eq!(blob_u32(code, 1), Some(0x0001_0300));
            }
        }
    }
}

unsafe extern "C" fn load_shared_file(
    user_data: *mut c_void,
    normalized_path: *const std::ffi::c_char,
    out_file: *mut Blob,
) -> Status {
    if user_data.is_null() || normalized_path.is_null() || out_file.is_null() {
        return STATUS_INVALID_ARGUMENT;
    }
    let path = unsafe { CStr::from_ptr(normalized_path) };
    if path.to_bytes() != b"shared.hlsl" {
        return STATUS_NOT_FOUND;
    }
    let calls = unsafe { &*(user_data.cast::<AtomicUsize>()) };
    calls.fetch_add(1, Ordering::Relaxed);
    unsafe {
        *out_file = Blob {
            data: SHARED_SOURCE.as_ptr(),
            size: SHARED_SOURCE.len(),
        };
    }
    STATUS_OK
}

#[test]
fn project_owned_abi_is_callable_from_rust() {
    unsafe {
        assert_eq!(slang_slim_abi_version(), ABI_VERSION);

        let mut compiler: *mut Compiler = ptr::null_mut();
        assert_eq!(slang_slim_compiler_create(&mut compiler), STATUS_OK);
        assert!(!compiler.is_null());
        #[cfg(not(target_os = "android"))]
        {
            assert_eq!(
                slang_slim_compiler_supports_target(compiler, TARGET_HLSL),
                1
            );
            assert_eq!(
                slang_slim_compiler_supports_target(compiler, TARGET_METAL),
                1
            );
        }
        assert_eq!(
            slang_slim_compiler_supports_target(compiler, TARGET_SPIRV),
            1
        );
        assert_eq!(
            slang_slim_compiler_supports_target_format(compiler, COMPILE_TARGET_SPIRV, ptr::null()),
            1
        );
        #[cfg(not(target_os = "android"))]
        {
            assert_eq!(
                slang_slim_compiler_supports_target_format(
                    compiler,
                    COMPILE_TARGET_HLSL,
                    ptr::null()
                ),
                1
            );
            assert_eq!(
                slang_slim_compiler_supports_target_format(
                    compiler,
                    COMPILE_TARGET_METAL,
                    ptr::null()
                ),
                1
            );
        }
        #[cfg(target_os = "android")]
        {
            assert_eq!(
                slang_slim_compiler_supports_target(compiler, TARGET_HLSL),
                0
            );
            assert_eq!(
                slang_slim_compiler_supports_target(compiler, TARGET_METAL),
                0
            );
        }
        slang_slim_compiler_destroy(compiler);
    }
}

#[test]
fn rust_ffi_compiles_multi_entry_targets_and_vfs() {
    unsafe {
        let mut compiler: *mut Compiler = ptr::null_mut();
        assert_eq!(slang_slim_compiler_create(&mut compiler), STATUS_OK);

        let (status, compilation) = compile_fixture(
            compiler,
            true,
            None,
            ptr::null_mut(),
            "rust_ffi_fixture_one",
        );
        assert_compile_success(status, compilation);
        assert_outputs(compilation, EXPECTED_TARGETS);
        slang_slim_compilation_destroy(compilation);

        let (status, compilation) = compile_fixture(
            compiler,
            true,
            None,
            ptr::null_mut(),
            "rust_ffi_fixture_two",
        );
        assert_compile_success(status, compilation);
        assert_outputs(compilation, EXPECTED_TARGETS);
        slang_slim_compilation_destroy(compilation);

        let (status, compilation) = compile_legacy_fixture(compiler);
        assert_compile_success(status, compilation);
        assert_outputs(compilation, &[TARGET_SPIRV]);
        slang_slim_compilation_destroy(compilation);

        let callback_calls = Box::new(AtomicUsize::new(0));
        let callback_user_data = (&*callback_calls as *const AtomicUsize).cast_mut().cast();
        let (status, compilation) = compile_fixture(
            compiler,
            false,
            Some(load_shared_file),
            callback_user_data,
            "rust_ffi_callback_fixture",
        );
        assert_compile_success(status, compilation);
        assert_outputs(compilation, EXPECTED_TARGETS);
        assert!(callback_calls.load(Ordering::Relaxed) > 0);
        slang_slim_compilation_destroy(compilation);

        slang_slim_compiler_destroy(compiler);
    }
}
