#![cfg(feature = "native")]

use slang_slim::{FileSystem, GlobalSession, SessionDesc, TargetDesc, sys};

const SHARED_SOURCE: &[u8] = b"float4 shared_tint() { return float4(1.0, 0.75, 0.5, 1.0); }\n";

const SOURCE: &[u8] = br#"
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

#[test]
fn safe_wrapper_follows_slang_component_flow() -> Result<(), Box<dyn std::error::Error>> {
    let global = GlobalSession::new()?;
    let hlsl_profile = global.find_profile("sm_6_0").expect("missing HLSL profile");
    let spirv_profile = global
        .find_profile("spirv_1_3")
        .expect("missing SPIR-V profile");

    #[cfg(not(target_os = "android"))]
    let metal_profile = global
        .find_profile("metallib_2_3")
        .expect("missing Metal profile");

    let file_system = FileSystem::new(|path: &str| {
        if path.ends_with("shared.hlsl") {
            Ok(SHARED_SOURCE.to_vec())
        } else {
            Err(sys::SLANG_E_NOT_FOUND)
        }
    })?;

    let session = {
        let mut session_desc = SessionDesc::new();
        #[cfg(not(target_os = "android"))]
        session_desc.add_target(TargetDesc::new(sys::SLANG_HLSL, hlsl_profile));

        session_desc.add_target(TargetDesc::new(sys::SLANG_SPIRV, spirv_profile));

        #[cfg(not(target_os = "android"))]
        session_desc.add_target(TargetDesc::new(sys::SLANG_METAL, metal_profile));
        session_desc.set_file_system(&file_system);
        global.create_session(&session_desc)?
    };

    // The descriptor's file system is retained by the session; this also
    // exercises the Rust callback-state keepalive in the safe wrapper.
    drop(file_system);

    let module = session
        .load_module_from_source("safe_flow", "safe_flow.hlsl", SOURCE)?
        .value;
    assert_eq!(module.name().as_deref(), Some("safe_flow"));
    assert!(module.file_path().is_some());

    let vertex = module.find_and_check_entry_point("vertex_main", sys::SLANG_STAGE_VERTEX)?;
    let fragment = module.find_and_check_entry_point("fragment_main", sys::SLANG_STAGE_FRAGMENT)?;
    let compute = module.find_and_check_entry_point("compute_main", sys::SLANG_STAGE_COMPUTE)?;
    let composite = session.create_composite_component_type(&[
        &vertex.value,
        &fragment.value,
        &compute.value,
    ])?;
    let linked = composite.value.link()?;

    #[cfg(target_os = "android")]
    let target_count = 1;
    #[cfg(not(target_os = "android"))]
    let target_count = 3;

    for target_index in 0..target_count {
        let code = linked.value.get_target_code(target_index)?;
        assert!(
            !code.value.is_empty(),
            "target {target_index} returned no code"
        );
        let layout = linked.value.get_layout(target_index)?;
        let json = layout.value.to_json_string()?;
        assert!(!json.value.is_empty());

        // The safe view follows Slang's original ProgramLayout -> entry point
        // and type/layout reflection flow. Child views borrow the layout and
        // keep its owner alive through their internal reference count.
        assert_eq!(layout.value.get_entry_point_count(), 3);
        let expected_entries = [
            ("vertex_main", sys::SLANG_STAGE_VERTEX),
            ("fragment_main", sys::SLANG_STAGE_FRAGMENT),
            ("compute_main", sys::SLANG_STAGE_COMPUTE),
        ];
        for (name, stage) in expected_entries {
            let entry = layout
                .value
                .find_entry_point_by_name(name)?
                .expect("entry point missing from reflection");
            assert_eq!(entry.get_name().as_deref(), Some(name));
            assert_eq!(entry.get_stage(), stage);
        }

        let compute = layout
            .value
            .find_entry_point_by_name("compute_main")?
            .expect("compute entry point missing from reflection");
        assert_eq!(compute.get_compute_thread_group_size(3)?, vec![1, 1, 1]);

        let output_type = layout
            .value
            .find_type_by_name("VertexOutput")?
            .expect("VertexOutput missing from reflection");
        assert_eq!(output_type.get_kind(), sys::SLANG_TYPE_KIND_STRUCT);
        assert_eq!(output_type.get_field_count(), 2);
        assert_eq!(
            output_type
                .get_field_by_index(0)
                .and_then(|field| field.get_name()),
            Some("position".to_owned())
        );
        assert_eq!(
            output_type
                .get_field_by_index(1)
                .and_then(|field| field.get_name()),
            Some("uv".to_owned())
        );

        let output_layout = layout
            .value
            .get_type_layout(&output_type, sys::SLANG_LAYOUT_RULES_DEFAULT)?
            .expect("VertexOutput layout missing from reflection");
        assert_eq!(output_layout.get_kind(), sys::SLANG_TYPE_KIND_STRUCT);
        assert_eq!(output_layout.get_field_count(), 2);
        assert!(output_layout.get_size(sys::SLANG_PARAMETER_CATEGORY_UNIFORM) > 0);
        let position_layout = output_layout
            .get_field_by_index(0)
            .expect("position layout missing");
        assert_eq!(
            position_layout
                .get_variable()
                .and_then(|variable| variable.get_name()),
            Some("position".to_owned())
        );
        assert!(position_layout.get_type_layout().is_some());
    }
    for entry_point_index in 0..3 {
        let code = linked.value.get_entry_point_code(entry_point_index, 0)?;
        assert!(!code.value.is_empty());
    }

    Ok(())
}
