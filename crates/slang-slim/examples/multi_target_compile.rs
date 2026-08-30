#[cfg(feature = "native-tests")]
use std::{
    error::Error,
    sync::{
        Arc,
        atomic::{AtomicUsize, Ordering},
    },
};

#[cfg(feature = "native-tests")]
use slang_slim::{Component, FileSystem, GlobalSession, SessionDesc, TargetDesc, sys};

#[cfg(not(feature = "native-tests"))]
fn main() {
    eprintln!("enable the `native-tests` feature to run this example");
}

#[cfg(feature = "native-tests")]
fn main() -> Result<(), Box<dyn Error>> {
    run()
}

#[cfg(feature = "native-tests")]
const SHARED_SHADER: &[u8] = br#"
float4 included_value() { return float4(1.0, 0.75, 0.5, 1.0); }
"#;

#[cfg(feature = "native-tests")]
const MAIN_SHADER: &[u8] = br#"
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
    return included_value() + float4(input.uv, 0.0, 0.0);
}

[shader("compute")]
[numthreads(1, 1, 1)]
void compute_main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
}
"#;

#[cfg(feature = "native-tests")]
fn run() -> Result<(), Box<dyn Error>> {
    let global = GlobalSession::new()?;

    #[cfg(target_os = "android")]
    let targets = {
        let spirv_profile = global
            .find_profile("spirv_1_3")
            .ok_or("the native Slang build does not provide spirv_1_3")?;
        vec![("SPIR-V 1.3", sys::SLANG_SPIRV, spirv_profile)]
    };

    #[cfg(not(target_os = "android"))]
    let targets = {
        let hlsl_profile = global
            .find_profile("sm_6_0")
            .ok_or("the native Slang build does not provide sm_6_0")?;
        let spirv_profile = global
            .find_profile("spirv_1_3")
            .ok_or("the native Slang build does not provide spirv_1_3")?;
        let metal_profile = global
            .find_profile("metallib_2_3")
            .ok_or("the native Slang build does not provide metallib_2_3")?;
        vec![
            ("HLSL SM 6.0", sys::SLANG_HLSL, hlsl_profile),
            ("SPIR-V 1.3", sys::SLANG_SPIRV, spirv_profile),
            ("MSL 2.3 source", sys::SLANG_METAL, metal_profile),
        ]
    };

    let include_requests = Arc::new(AtomicUsize::new(0));
    let include_requests_for_callback = Arc::clone(&include_requests);
    let file_system = FileSystem::new(move |path: &str| {
        if path.ends_with("shared.hlsl") {
            include_requests_for_callback.fetch_add(1, Ordering::Relaxed);
            Ok(SHARED_SHADER.to_vec())
        } else {
            Err(sys::SLANG_E_NOT_FOUND)
        }
    })?;

    let session = {
        let mut desc = SessionDesc::new();
        for (_, format, profile) in &targets {
            desc.add_target(TargetDesc::new(*format, *profile));
        }
        desc.set_file_system(&file_system);
        global.create_session(&desc)?
    };

    // Slang retains the native file-system handle through the session. The
    // Rust callback state is retained by the safe wrapper for the same span.
    drop(file_system);

    let module = session
        .load_module_from_source(
            "multi_target_compile",
            "multi_target_compile.hlsl",
            MAIN_SHADER,
        )?
        .value;
    assert_eq!(module.name().as_deref(), Some("multi_target_compile"));
    assert!(module.file_path().is_some());
    let vertex = module
        .find_and_check_entry_point("vertex_main", sys::SLANG_STAGE_VERTEX)?
        .value;
    let fragment = module
        .find_and_check_entry_point("fragment_main", sys::SLANG_STAGE_FRAGMENT)?
        .value;
    let compute = module
        .find_and_check_entry_point("compute_main", sys::SLANG_STAGE_COMPUTE)?
        .value;
    let entry_points = [&vertex, &fragment, &compute];
    let components: Vec<&dyn Component> = entry_points
        .iter()
        .map(|entry_point| *entry_point as &dyn Component)
        .collect();
    let composite = session.create_composite_component_type(&components)?.value;
    let linked = composite.link()?.value;
    assert!(include_requests.load(Ordering::Relaxed) > 0);
    for (target_index, (name, _, _)) in targets.iter().enumerate() {
        let target_index = sys::SlangInt::try_from(target_index)?;
        let code = linked.get_target_code(target_index)?;
        if code.value.is_empty() {
            return Err(format!("{name} returned no target code").into());
        }

        let layout = linked.get_layout(target_index)?;
        let reflection = layout.value.to_json_string()?;
        if !reflection.value.contains("compute_main") {
            return Err(format!("{name} reflection omitted compute_main").into());
        }

        println!(
            "{name}: target={} bytes, {} entry points, reflection={} bytes",
            code.value.len(),
            entry_points.len(),
            reflection.value.len()
        );
    }

    let spirv_target_index = targets
        .iter()
        .position(|(_, format, _)| *format == sys::SLANG_SPIRV)
        .ok_or("no SPIR-V target configured")?;
    for (entry_point_index, _) in entry_points.iter().enumerate() {
        let entry_point_code = linked.get_entry_point_code(
            sys::SlangInt::try_from(entry_point_index)?,
            sys::SlangInt::try_from(spirv_target_index)?,
        )?;
        if entry_point_code.value.is_empty() {
            return Err("SPIR-V returned no entry-point code".into());
        }
        let header = entry_point_code
            .value
            .get(..8)
            .ok_or("SPIR-V output is shorter than its header")?;
        if header[..4] != 0x0723_0203u32.to_le_bytes()
            || header[4..8] != 0x0001_0300u32.to_le_bytes()
        {
            return Err("SPIR-V output is not version 1.3".into());
        }
    }

    Ok(())
}
