#![cfg(feature = "native")]

use std::panic::{AssertUnwindSafe, catch_unwind};

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
fn safe_vfs_catches_callback_panics() -> Result<(), Box<dyn std::error::Error>> {
    let file_system = FileSystem::new(
        |_path: &str| -> std::result::Result<Vec<u8>, sys::SlangResult> {
            panic!("intentional VFS callback panic")
        },
    )?;
    let session = create_spirv_session(&file_system)?;
    let result = catch_unwind(AssertUnwindSafe(|| {
        session.load_module_from_source(
            "vfs_panic",
            "pkg/main_panic.hlsl",
            br#"#include "panic.hlsl"
float4 main_value() { return 1.0.xxxx; }
"#,
        )
    }));
    let result = result.expect("VFS callback panic crossed the Rust API boundary");
    let error = match result {
        Ok(_) => panic!("a panicking VFS callback must fail module loading"),
        Err(error) => error,
    };
    assert_eq!(error.status(), sys::SLANG_FAIL);
    assert!(error.diagnostics().to_string_lossy().contains("panic.hlsl"));
    Ok(())
}
