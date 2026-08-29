include_guard(GLOBAL)

function(slang_slim_configure_slang)
    # Slang is linked into the future slang-slim wrapper. Keep the dependency
    # static and position-independent so Android needs only one public .so.
    set(CMAKE_POSITION_INDEPENDENT_CODE ON CACHE BOOL "Build position-independent native code" FORCE)
    set(SLANG_LIB_TYPE STATIC CACHE STRING "Build Slang as a static dependency" FORCE)
    set(SLANG_SLANG_LLVM_FLAVOR DISABLE CACHE STRING "LLVM targets are outside slang-slim scope" FORCE)

    set(SLANG_EMBED_CORE_MODULE ON CACHE BOOL "Embed Slang's core module" FORCE)
    set(SLANG_EMBED_CORE_MODULE_SOURCE ON CACHE BOOL "Embed Slang's core module source" FORCE)
    set(SLANG_STANDARD_MODULE_DEVELOP_BUILD OFF CACHE BOOL "Use release standard modules" FORCE)

    # slang-slim accepts HLSL and uses Slang's native HLSL, SPIR-V and Metal
    # emitters. It does not need DXC, glslang, LLVM, rendering APIs, tools, or
    # the backward-compatibility proxy library.
    set(SLANG_ENABLE_DXIL OFF CACHE BOOL "DXIL generation is outside slang-slim scope" FORCE)
    set(SLANG_ENABLE_GFX OFF CACHE BOOL "Rendering APIs are outside slang-slim scope" FORCE)
    set(SLANG_ENABLE_SLANG_RHI OFF CACHE BOOL "slang-rhi is outside slang-slim scope" FORCE)
    set(SLANG_ENABLE_SLANG_GLSLANG OFF CACHE BOOL "GLSL input is outside slang-slim scope" FORCE)
    set(SLANG_ENABLE_SLANGC OFF CACHE BOOL "The standalone compiler is not distributed" FORCE)
    set(SLANG_ENABLE_SLANGD OFF CACHE BOOL "The language server is not distributed" FORCE)
    set(SLANG_ENABLE_SLANGI OFF CACHE BOOL "The interpreter is not distributed" FORCE)
    set(SLANG_ENABLE_SLANGRT OFF CACHE BOOL "The CPU runtime is outside slang-slim scope" FORCE)
    set(SLANG_ENABLE_SLANG_PROXY OFF CACHE BOOL "The legacy Slang proxy is not distributed" FORCE)
    set(SLANG_ENABLE_TESTS OFF CACHE BOOL "Upstream tests are not part of production builds" FORCE)
    set(SLANG_ENABLE_EXAMPLES OFF CACHE BOOL "Upstream examples are not part of production builds" FORCE)
    set(SLANG_ENABLE_REPLAYER OFF CACHE BOOL "The replay tool is outside slang-slim scope" FORCE)

    set(SLANG_ENABLE_CUDA OFF CACHE BOOL "CUDA is outside slang-slim scope" FORCE)
    set(SLANG_ENABLE_OPTIX OFF CACHE BOOL "OptiX is outside slang-slim scope" FORCE)
    set(SLANG_ENABLE_NVAPI OFF CACHE BOOL "NVAPI is outside slang-slim scope" FORCE)
    set(SLANG_ENABLE_AFTERMATH OFF CACHE BOOL "Aftermath is outside slang-slim scope" FORCE)
    set(SLANG_ENABLE_DX_ON_VK OFF CACHE BOOL "DirectX-on-Vulkan is outside slang-slim scope" FORCE)

    set(SLANG_EXCLUDE_DAWN ON CACHE BOOL "Do not fetch Dawn" FORCE)
    set(SLANG_EXCLUDE_TINT ON CACHE BOOL "Do not fetch slang-tint" FORCE)
    set(SLANG_ENABLE_MIMALLOC OFF CACHE BOOL "Use the platform allocator" FORCE)
    set(SLANG_ENABLE_SPIRV_TOOLS_MIMALLOC OFF CACHE BOOL "SPIRV-Tools is not built" FORCE)
    set(SLANG_ENABLE_RELEASE_DEBUG_INFO OFF CACHE BOOL "Release artifacts omit debug information" FORCE)
    set(SLANG_ENABLE_SPLIT_DEBUG_INFO OFF CACHE BOOL "Release artifacts omit split debug information" FORCE)
endfunction()
