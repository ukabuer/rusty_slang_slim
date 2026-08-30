#![cfg(feature = "native-tests")]

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
        session_desc.add_target(TargetDesc::new(sys::SLANG_HLSL, hlsl_profile));
        session_desc.add_target(TargetDesc::new(sys::SLANG_SPIRV, spirv_profile));
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

    for target_index in 0..3 {
        let code = linked.value.get_target_code(target_index)?;
        assert!(
            !code.value.is_empty(),
            "target {target_index} returned no code"
        );
        let layout = linked.value.get_layout(target_index)?;
        let json = layout.value.to_json_string()?;
        assert!(!json.value.is_empty());
    }
    for entry_point_index in 0..3 {
        let code = linked.value.get_entry_point_code(entry_point_index, 0)?;
        assert!(!code.value.is_empty());
    }

    Ok(())
}
