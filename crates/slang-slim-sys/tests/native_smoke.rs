#![cfg(slang_slim_native_linked)]

use core::mem::size_of;
use core::ptr;
use std::ffi::{CStr, CString, c_void};
use std::sync::atomic::{AtomicUsize, Ordering};

use slang_slim_sys::{
    ABI_VERSION, FileSystem, IBlob, K_DEFAULT_TARGET_FLAGS, Module, ProgramLayout,
    SLANG_C_API_ABI_VERSION, SLANG_E_INVALID_ARG, SLANG_E_NOT_FOUND, SLANG_HLSL,
    SLANG_LANGUAGE_VERSION_2025, SLANG_LAYOUT_RULES_DEFAULT, SLANG_METAL, SLANG_PROFILE_UNKNOWN,
    SLANG_SPIRV, SLANG_STAGE_COMPUTE, SLANG_STAGE_FRAGMENT, SLANG_STAGE_VERTEX,
    SLANG_TYPE_KIND_STRUCT, Session, SlangCompileTarget, SlangFileSystemDesc,
    SlangGlobalSessionDesc, SlangLoadFileFunc, SlangResult, SlangSessionDesc, SlangStage,
    SlangTargetDesc, slang_abi_version, slang_blob_destroy, slang_blob_get_buffer_pointer,
    slang_blob_get_buffer_size, slang_component_type_destroy,
    slang_component_type_get_entry_point_code, slang_component_type_get_layout,
    slang_component_type_link, slang_create_blob, slang_create_global_session,
    slang_file_system_create, slang_file_system_destroy, slang_global_session_create_session,
    slang_global_session_destroy, slang_global_session_find_profile,
    slang_global_session_get_build_tag, slang_module_find_and_check_entry_point,
    slang_module_get_file_path, slang_module_get_name, slang_program_layout_destroy,
    slang_program_layout_get_reflection,
    slang_reflection_entry_point_get_compute_thread_group_size,
    slang_reflection_entry_point_get_name, slang_reflection_entry_point_get_stage,
    slang_reflection_find_type_by_name, slang_reflection_get_entry_point_by_index,
    slang_reflection_get_entry_point_count, slang_reflection_get_parameter_count,
    slang_reflection_get_type_layout, slang_reflection_to_json,
    slang_reflection_type_get_field_by_index, slang_reflection_type_get_field_count,
    slang_reflection_type_get_kind, slang_reflection_type_get_name,
    slang_reflection_type_layout_get_field_count, slang_reflection_type_layout_get_kind,
    slang_reflection_type_layout_get_size, slang_session_create_composite_component_type,
    slang_session_destroy, slang_session_load_module_from_source,
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

fn raw_blob_text(blob: *mut IBlob) -> String {
    let data = unsafe { slang_blob_get_buffer_pointer(blob) };
    let size = unsafe { slang_blob_get_buffer_size(blob) };
    if data.is_null() || size == 0 {
        return String::new();
    }
    let bytes = unsafe { core::slice::from_raw_parts(data.cast::<u8>(), size) };
    String::from_utf8_lossy(bytes).into_owned()
}

unsafe fn release_blob(blob: &mut *mut IBlob) {
    if !(*blob).is_null() {
        unsafe { slang_blob_destroy(*blob) };
        *blob = ptr::null_mut();
    }
}

unsafe fn raw_assert_success(status: SlangResult, diagnostics: *mut IBlob, context: &str) {
    if status >= 0 {
        let mut diagnostics = diagnostics;
        unsafe { release_blob(&mut diagnostics) };
        return;
    }
    let message = raw_blob_text(diagnostics);
    let mut diagnostics = diagnostics;
    unsafe { release_blob(&mut diagnostics) };
    panic!("native Slang API call {context} failed with {status}: {message}");
}

unsafe extern "C" fn load_shared_blob(
    user_data: *mut c_void,
    path: *const std::ffi::c_char,
    out_blob: *mut *mut IBlob,
) -> SlangResult {
    if user_data.is_null() || path.is_null() || out_blob.is_null() {
        return SLANG_E_INVALID_ARG;
    }
    let path = unsafe { CStr::from_ptr(path) };
    if path.to_bytes() != b"shared.hlsl" && path.to_bytes() != b"abi/shared.hlsl" {
        return SLANG_E_NOT_FOUND;
    }
    let calls = unsafe { &*(user_data.cast::<AtomicUsize>()) };
    calls.fetch_add(1, Ordering::Relaxed);
    unsafe { slang_create_blob(SHARED_SOURCE.as_ptr().cast(), SHARED_SOURCE.len(), out_blob) }
}

fn make_target(format: SlangCompileTarget, profile: u32) -> SlangTargetDesc {
    SlangTargetDesc {
        structure_size: size_of::<SlangTargetDesc>(),
        format,
        profile,
        flags: K_DEFAULT_TARGET_FLAGS,
        floating_point_mode: 0,
        line_directive_mode: 0,
        force_glsl_scalar_buffer_layout: 0,
        _force_glsl_scalar_buffer_layout_padding: [0; 3],
        compiler_option_entries: ptr::null(),
        compiler_option_entry_count: 0,
    }
}

#[test]
fn slang_c_api_is_callable_from_rust() {
    unsafe {
        assert_eq!(slang_abi_version(), ABI_VERSION);
        assert_eq!(ABI_VERSION, SLANG_C_API_ABI_VERSION);

        // The public Slang slang_createBlob helper rejects zero-byte buffers,
        // but an ISlangFileSystem must be able to return an empty file.
        let mut empty_blob = ptr::null_mut();
        assert_eq!(
            slang_create_blob(ptr::null(), 0, &mut empty_blob),
            slang_slim_sys::SLANG_OK
        );
        assert!(!empty_blob.is_null());
        assert_eq!(slang_blob_get_buffer_size(empty_blob), 0);
        slang_blob_destroy(empty_blob);

        let global_desc = SlangGlobalSessionDesc {
            structure_size: size_of::<SlangGlobalSessionDesc>() as u32,
            api_version: 0,
            min_language_version: SLANG_LANGUAGE_VERSION_2025,
            enable_glsl: 0,
            _enable_glsl_padding: [0; 3],
            reserved: [0; 16],
        };
        let mut global = ptr::null_mut();
        raw_assert_success(
            slang_create_global_session(&global_desc, &mut global),
            ptr::null_mut(),
            "create global session",
        );
        assert!(!global.is_null());
        assert!(!slang_global_session_get_build_tag(global).is_null());

        let hlsl_profile =
            slang_global_session_find_profile(global, CString::new("sm_6_0").unwrap().as_ptr());
        let spirv_profile =
            slang_global_session_find_profile(global, CString::new("spirv_1_3").unwrap().as_ptr());
        #[cfg(not(target_os = "android"))]
        let metal_profile = slang_global_session_find_profile(
            global,
            CString::new("metallib_2_3").unwrap().as_ptr(),
        );
        assert_ne!(hlsl_profile, SLANG_PROFILE_UNKNOWN);
        assert_ne!(spirv_profile, SLANG_PROFILE_UNKNOWN);
        #[cfg(not(target_os = "android"))]
        assert_ne!(metal_profile, SLANG_PROFILE_UNKNOWN);

        let raw_callback_calls = Box::new(AtomicUsize::new(0));
        let raw_callback_user = (&*raw_callback_calls as *const AtomicUsize)
            .cast_mut()
            .cast();
        let file_system_desc = SlangFileSystemDesc {
            structure_size: size_of::<SlangFileSystemDesc>(),
            load_file: Some(load_shared_blob as SlangLoadFileFunc),
            load_file_user_data: raw_callback_user,
        };
        let mut file_system = ptr::null_mut::<FileSystem>();
        raw_assert_success(
            slang_file_system_create(&file_system_desc, &mut file_system),
            ptr::null_mut(),
            "create file system",
        );

        let mut raw_targets = Vec::new();
        raw_targets.push(make_target(SLANG_SPIRV, spirv_profile));
        #[cfg(not(target_os = "android"))]
        {
            raw_targets.insert(0, make_target(SLANG_HLSL, hlsl_profile));
            raw_targets.push(make_target(SLANG_METAL, metal_profile));
        }

        let session_desc = SlangSessionDesc {
            structure_size: size_of::<SlangSessionDesc>(),
            targets: raw_targets.as_ptr(),
            target_count: raw_targets.len() as i64,
            flags: 0,
            default_matrix_layout_mode: 1,
            search_paths: ptr::null(),
            search_path_count: 0,
            preprocessor_macros: ptr::null(),
            preprocessor_macro_count: 0,
            file_system,
            enable_effect_annotations: 0,
            allow_glsl_syntax: 0,
            _session_bool_padding: [0; 6],
            compiler_option_entries: ptr::null(),
            compiler_option_entry_count: 0,
            skip_spirv_validation: 0,
            _skip_spirv_validation_padding: [0; 3],
        };
        let mut session = ptr::null_mut::<Session>();
        raw_assert_success(
            slang_global_session_create_session(global, &session_desc, &mut session),
            ptr::null_mut(),
            "create session",
        );
        // Slang retains the file-system interface from the session descriptor;
        // the caller's original reference can be released immediately.
        slang_file_system_destroy(file_system);
        file_system = ptr::null_mut();

        let module_name = CString::new("raw_slang_c_api").unwrap();
        let source_path = CString::new("main.hlsl").unwrap();
        let mut source_blob = ptr::null_mut::<IBlob>();
        raw_assert_success(
            slang_create_blob(SOURCE.as_ptr().cast(), SOURCE.len(), &mut source_blob),
            ptr::null_mut(),
            "create source blob",
        );
        let mut diagnostics = ptr::null_mut::<IBlob>();
        let mut module = ptr::null_mut::<Module>();
        let status = slang_session_load_module_from_source(
            session,
            module_name.as_ptr(),
            source_path.as_ptr(),
            source_blob,
            &mut diagnostics,
            &mut module,
        );
        release_blob(&mut source_blob);
        raw_assert_success(status, diagnostics, "load module");
        assert!(!module.is_null());
        assert_eq!(
            CStr::from_ptr(slang_module_get_name(module)).to_bytes(),
            b"raw_slang_c_api"
        );
        assert_eq!(
            CStr::from_ptr(slang_module_get_file_path(module)).to_bytes(),
            b"main.hlsl"
        );

        let entry_names = [
            CString::new("vertex_main").unwrap(),
            CString::new("fragment_main").unwrap(),
            CString::new("compute_main").unwrap(),
        ];
        let stages: [SlangStage; 3] = [
            SLANG_STAGE_VERTEX,
            SLANG_STAGE_FRAGMENT,
            SLANG_STAGE_COMPUTE,
        ];
        let mut entries = Vec::new();
        for (name, stage) in entry_names.iter().zip(stages) {
            let mut entry = ptr::null_mut::<Module>();
            diagnostics = ptr::null_mut();
            let status = slang_module_find_and_check_entry_point(
                module,
                name.as_ptr(),
                stage,
                &mut entry,
                &mut diagnostics,
            );
            raw_assert_success(status, diagnostics, "find entry point");
            assert!(!entry.is_null());
            entries.push(entry);
        }

        let mut components: Vec<*mut slang_slim_sys::IComponentType> =
            Vec::with_capacity(entries.len() + 1);
        components.push(module.cast());
        components.extend(entries.iter().copied().map(|entry| entry.cast()));
        let mut program = ptr::null_mut::<slang_slim_sys::ComponentType>();
        diagnostics = ptr::null_mut();
        let status = slang_session_create_composite_component_type(
            session,
            components.as_ptr(),
            components.len() as i64,
            &mut program,
            &mut diagnostics,
        );
        raw_assert_success(status, diagnostics, "create composite component");
        assert!(!program.is_null());

        let mut linked = ptr::null_mut::<slang_slim_sys::ComponentType>();
        diagnostics = ptr::null_mut();
        let status = slang_component_type_link(program, &mut linked, &mut diagnostics);
        raw_assert_success(status, diagnostics, "link component");
        assert!(!linked.is_null());

        // A ProgramLayout owns a reference to the linked component, while the
        // reflection records returned from it are borrowed. Keep every layout
        // alive until all of its borrowed records have been queried.
        let mut layouts = Vec::with_capacity(raw_targets.len());
        for (target_index, target_desc) in raw_targets.iter().enumerate() {
            let mut layout = ptr::null_mut::<ProgramLayout>();
            diagnostics = ptr::null_mut();
            let status = slang_component_type_get_layout(
                linked,
                target_index as i64,
                &mut layout,
                &mut diagnostics,
            );
            raw_assert_success(status, diagnostics, "get layout");
            assert!(!layout.is_null());
            layouts.push(layout);

            // The sys crate exposes the original Slang reflection C symbols
            // through bridge functions backed by Slang's C++ reflection API.
            // The returned records are borrowed from the layout and must not
            // be released independently.
            let raw_reflection = slang_program_layout_get_reflection(layout);
            assert!(!raw_reflection.is_null());
            assert_eq!(slang_reflection_get_parameter_count(raw_reflection), 0);
            let mut direct_json = ptr::null_mut::<IBlob>();
            raw_assert_success(
                slang_reflection_to_json(raw_reflection, &mut direct_json),
                ptr::null_mut(),
                "direct reflection JSON",
            );
            assert!(raw_blob_text(direct_json).contains("compute_main"));
            release_blob(&mut direct_json);
            assert_eq!(slang_reflection_get_entry_point_count(raw_reflection), 3);
            for (index, (name, stage)) in [
                ("vertex_main", SLANG_STAGE_VERTEX),
                ("fragment_main", SLANG_STAGE_FRAGMENT),
                ("compute_main", SLANG_STAGE_COMPUTE),
            ]
            .into_iter()
            .enumerate()
            {
                let entry = slang_reflection_get_entry_point_by_index(raw_reflection, index as u64);
                assert!(!entry.is_null());
                assert_eq!(
                    CStr::from_ptr(slang_reflection_entry_point_get_name(entry)).to_bytes(),
                    name.as_bytes()
                );
                assert_eq!(slang_reflection_entry_point_get_stage(entry), stage);
                if stage == SLANG_STAGE_COMPUTE {
                    let mut group_size = [0_u64; 3];
                    slang_reflection_entry_point_get_compute_thread_group_size(
                        entry,
                        group_size.len() as u64,
                        group_size.as_mut_ptr(),
                    );
                    assert_eq!(group_size, [1, 1, 1]);
                }
            }

            let type_name = CString::new("VertexOutput").unwrap();
            let reflected_type =
                slang_reflection_find_type_by_name(raw_reflection, type_name.as_ptr());
            assert!(!reflected_type.is_null());
            assert_eq!(
                slang_reflection_type_get_kind(reflected_type),
                SLANG_TYPE_KIND_STRUCT
            );
            assert_eq!(
                CStr::from_ptr(slang_reflection_type_get_name(reflected_type)).to_bytes(),
                b"VertexOutput"
            );
            assert_eq!(slang_reflection_type_get_field_count(reflected_type), 2);
            let first_field = slang_reflection_type_get_field_by_index(reflected_type, 0);
            assert!(!first_field.is_null());
            assert_eq!(
                CStr::from_ptr(slang_slim_sys::slang_reflection_variable_get_name(
                    first_field
                ))
                .to_bytes(),
                b"position"
            );
            let type_layout = slang_reflection_get_type_layout(
                raw_reflection,
                reflected_type,
                SLANG_LAYOUT_RULES_DEFAULT,
            );
            assert!(!type_layout.is_null());
            assert_eq!(
                slang_reflection_type_layout_get_kind(type_layout),
                SLANG_TYPE_KIND_STRUCT
            );
            assert_eq!(slang_reflection_type_layout_get_field_count(type_layout), 2);
            assert!(slang_reflection_type_layout_get_size(type_layout, 8) > 0);

            for entry_index in 0..entries.len() {
                let mut code = ptr::null_mut::<IBlob>();
                diagnostics = ptr::null_mut();
                let status = slang_component_type_get_entry_point_code(
                    linked,
                    entry_index as i64,
                    target_index as i64,
                    &mut code,
                    &mut diagnostics,
                );
                raw_assert_success(status, diagnostics, "entry point code");
                assert!(!code.is_null());
                assert!(slang_blob_get_buffer_size(code) > 0);
                if target_desc.format == SLANG_SPIRV {
                    let data = slang_blob_get_buffer_pointer(code).cast::<u8>();
                    let bytes = core::slice::from_raw_parts(data, 8);
                    assert_eq!(
                        u32::from_le_bytes(bytes[0..4].try_into().unwrap()),
                        0x0723_0203
                    );
                    assert_eq!(
                        u32::from_le_bytes(bytes[4..8].try_into().unwrap()),
                        0x0001_0300
                    );
                }
                release_blob(&mut code);
            }
        }

        for layout in layouts {
            slang_program_layout_destroy(layout);
        }
        slang_component_type_destroy(linked);
        slang_component_type_destroy(program);
        for entry in entries {
            slang_component_type_destroy(entry.cast());
        }
        slang_component_type_destroy(module.cast());
        slang_session_destroy(session);
        slang_file_system_destroy(file_system);
        assert!(raw_callback_calls.load(Ordering::Relaxed) > 0);
        slang_global_session_destroy(global);
    }
}
