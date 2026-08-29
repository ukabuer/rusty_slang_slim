#![cfg(slang_slim_native_linked)]

use core::ptr;
use slang_slim_sys::{
    ABI_VERSION, Compiler, STATUS_OK, TARGET_HLSL, TARGET_METAL, TARGET_SPIRV,
    slang_slim_abi_version, slang_slim_compiler_create, slang_slim_compiler_destroy,
    slang_slim_compiler_supports_target,
};

#[test]
fn project_owned_abi_is_callable_from_rust() {
    unsafe {
        assert_eq!(slang_slim_abi_version(), ABI_VERSION);

        let mut compiler: *mut Compiler = ptr::null_mut();
        assert_eq!(slang_slim_compiler_create(&mut compiler), STATUS_OK);
        assert!(!compiler.is_null());
        assert_eq!(
            slang_slim_compiler_supports_target(compiler, TARGET_HLSL),
            1
        );
        assert_eq!(
            slang_slim_compiler_supports_target(compiler, TARGET_SPIRV),
            1
        );
        assert_eq!(
            slang_slim_compiler_supports_target(compiler, TARGET_METAL),
            1
        );

        slang_slim_compiler_destroy(compiler);
    }
}
