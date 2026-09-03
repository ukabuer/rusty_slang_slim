#![cfg(feature = "native")]

use slang_slim::{FileSystem, GlobalSession, Session, SessionDesc, TargetDesc, sys};

fn create_spirv_session(file_system: &FileSystem) -> slang_slim::Result<Session> {
    let global = GlobalSession::new()?;
    let spirv_profile = global
        .find_profile("spirv_1_3")
        .expect("missing SPIR-V profile");
    let mut session_desc = SessionDesc::new();
    session_desc.add_target(TargetDesc::new(sys::SLANG_SPIRV, spirv_profile));
    session_desc.set_file_system(file_system);
    global.create_session(&session_desc)
}

#[test]
fn safe_vfs_maps_loader_errors_to_diagnostics() -> Result<(), Box<dyn std::error::Error>> {
    let file_system = FileSystem::new(|_path: &str| Err(sys::SLANG_E_CANNOT_OPEN))?;
    let session = create_spirv_session(&file_system)?;
    let result = session.load_module_from_source(
        "vfs_error_mapping",
        "pkg/main_error.hlsl",
        br#"#include "missing.hlsl"
float4 main_value() { return 1.0.xxxx; }
"#,
    );
    let error = match result {
        Ok(_) => panic!("an include failure must fail module loading"),
        Err(error) => error,
    };
    // The pointer-returning Slang loadModuleFromSource API has no status
    // result. The bridge reports SLANG_FAIL while preserving the VFS failure
    // in the diagnostic blob.
    assert_eq!(error.status(), sys::SLANG_FAIL);
    assert!(
        error
            .diagnostics()
            .to_string_lossy()
            .contains("missing.hlsl")
    );
    Ok(())
}
