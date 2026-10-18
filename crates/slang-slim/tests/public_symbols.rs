#![cfg(feature = "native")]

use slang_slim::{
    COMPILER_OPTION_EMIT_REFLECTION_JSON, COMPILER_OPTION_EMIT_SPIRV_DIRECTLY, CompilerOption,
    CompilerOptionName, GlobalSessionDesc, K_DEFAULT_TARGET_FLAGS, K_SESSION_FLAGS_NONE,
    SLANG_API_VERSION, SLANG_E_NOT_FOUND, SLANG_FLOATING_POINT_MODE_PRECISE,
    SLANG_LANGUAGE_VERSION_2026, SLANG_LINE_DIRECTIVE_MODE_NONE, SLANG_MATRIX_LAYOUT_COLUMN_MAJOR,
    SLANG_PROFILE_UNKNOWN, SLANG_SPIRV, SLANG_TARGET_FLAG_GENERATE_WHOLE_PROGRAM, SessionDesc,
    SessionFlags, SlangResult, TargetDesc, VirtualFileSystem,
};

// These consumer-facing operations must work without importing the raw crate
// or its `sys` re-export.
#[test]
fn descriptors_can_be_configured_with_safe_crate_symbols() {
    let global = GlobalSessionDesc {
        api_version: SLANG_API_VERSION,
        min_language_version: SLANG_LANGUAGE_VERSION_2026,
        ..GlobalSessionDesc::default()
    };
    let mut target = TargetDesc::new(SLANG_SPIRV, SLANG_PROFILE_UNKNOWN);
    target.flags = K_DEFAULT_TARGET_FLAGS | SLANG_TARGET_FLAG_GENERATE_WHOLE_PROGRAM;
    target.floating_point_mode = SLANG_FLOATING_POINT_MODE_PRECISE;
    target.line_directive_mode = SLANG_LINE_DIRECTIVE_MODE_NONE;

    let option_name: CompilerOptionName = COMPILER_OPTION_EMIT_SPIRV_DIRECTLY;
    target.add_compiler_option(CompilerOption::int(option_name, 1));

    let flags: SessionFlags = K_SESSION_FLAGS_NONE;
    let mut session = SessionDesc::new();
    session.flags = flags;
    session.default_matrix_layout_mode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;
    session.add_compiler_option(CompilerOption::int(COMPILER_OPTION_EMIT_REFLECTION_JSON, 1));
    session.add_target(target);

    assert_eq!(global.min_language_version, 2026);
    assert_eq!(session.targets[0].format, SLANG_SPIRV);
    assert_eq!(
        session.targets[0].compiler_options,
        [CompilerOption::int(COMPILER_OPTION_EMIT_SPIRV_DIRECTLY, 1)]
    );
    assert_eq!(
        session.compiler_options,
        [CompilerOption::int(COMPILER_OPTION_EMIT_REFLECTION_JSON, 1)]
    );
}

#[test]
fn virtual_file_system_can_return_safe_crate_status_codes() {
    let file_system = |path: &str| -> Result<Vec<u8>, SlangResult> {
        match path {
            "shared.hlsl" => Ok(b"float value;".to_vec()),
            _ => Err(SLANG_E_NOT_FOUND),
        }
    };

    assert_eq!(
        file_system.load_file("shared.hlsl"),
        Ok(b"float value;".to_vec())
    );
    assert_eq!(
        file_system.load_file("missing.hlsl"),
        Err(SLANG_E_NOT_FOUND)
    );
}
