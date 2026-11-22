#![cfg(feature = "native")]

use std::panic::{AssertUnwindSafe, catch_unwind};
use std::sync::{Arc, Mutex};

use slang_slim::{
    FileSystem, GlobalSession, SLANG_E_CANNOT_OPEN, SLANG_E_NOT_FOUND, SLANG_FAIL, SLANG_SPIRV,
    Session, SessionDesc, SlangResult, TargetDesc,
};

fn create_spirv_session(file_system: &FileSystem) -> slang_slim::Result<Session> {
    let global = GlobalSession::new()?;
    let spirv_profile = global
        .find_profile("spirv_1_3")
        .expect("missing SPIR-V profile");
    let mut session_desc = SessionDesc::new();
    session_desc.add_target(TargetDesc::new(SLANG_SPIRV, spirv_profile));
    session_desc.set_file_system(file_system);
    global.create_session(&session_desc)
}

#[test]
fn safe_vfs_normalizes_paths_and_caches_contents() -> Result<(), Box<dyn std::error::Error>> {
    const SOURCE_WITH_VARIANTS: &[u8] = br#"
#include "./sub/../shared.hlsl"
#include "shared.hlsl"
float4 main_value() { return 1.0.xxxx; }
"#;
    // The include is intentionally repeated without #pragma once so the
    // callback count observes Slang's CacheFileSystem rather than a
    // preprocessor-only short circuit.
    const SHARED_SOURCE: &[u8] = b"// shared source\n";

    let requested_paths = Arc::new(Mutex::new(Vec::<String>::new()));
    let requested_paths_for_callback = Arc::clone(&requested_paths);
    let file_system = FileSystem::new(move |path: &str| {
        requested_paths_for_callback
            .lock()
            .expect("path log mutex poisoned")
            .push(path.to_owned());
        if path == "pkg/shared.hlsl" {
            Ok(SHARED_SOURCE.to_vec())
        } else {
            Err(SLANG_E_NOT_FOUND)
        }
    })?;

    let session = create_spirv_session(&file_system)?;

    // Slang simplifies both include spellings before consulting the callback,
    // then reuses one cached file for the equivalent paths.
    let module =
        session.load_module_from_source("vfs_path_cache", "pkg/main.hlsl", SOURCE_WITH_VARIANTS)?;
    assert!(
        module.diagnostics.is_empty(),
        "unexpected diagnostics: {}",
        module.diagnostics
    );

    {
        let requested_paths = requested_paths.lock().expect("path log mutex poisoned");
        assert_eq!(requested_paths.as_slice(), ["pkg/shared.hlsl"]);
    }

    Ok(())
}

#[test]
fn safe_vfs_maps_loader_errors_to_diagnostics() -> Result<(), Box<dyn std::error::Error>> {
    let file_system = FileSystem::new(|_path: &str| Err(SLANG_E_CANNOT_OPEN))?;
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
    assert_eq!(error.status(), SLANG_FAIL);
    assert!(
        error
            .diagnostics()
            .to_string_lossy()
            .contains("missing.hlsl")
    );
    Ok(())
}

#[test]
fn safe_vfs_catches_callback_panics() -> Result<(), Box<dyn std::error::Error>> {
    let file_system =
        FileSystem::new(|_path: &str| -> std::result::Result<Vec<u8>, SlangResult> {
            panic!("intentional VFS callback panic")
        })?;
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
    assert_eq!(error.status(), SLANG_FAIL);
    assert!(error.diagnostics().to_string_lossy().contains("panic.hlsl"));
    Ok(())
}
