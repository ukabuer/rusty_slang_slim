cmake_minimum_required(VERSION 3.22)

project(slang_slim LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Do not let the Windows system code page change how vendored UTF-8 source is
# parsed. Some upstream dependencies promote encoding warnings to errors.
add_compile_options(
    "$<$<COMPILE_LANG_AND_ID:C,MSVC>:/utf-8>"
    "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>"
)

# The Android NDK injects `-g` even for Release configurations. Release assets
# keep symbols in a separate maintainer workflow, so make the production object
# archives deterministic in size without a required post-build mutation.
if(ANDROID)
    add_compile_options("$<$<CONFIG:Release>:-g0>")
endif()

set(
    SLANG_SLIM_SLANG_SOURCE_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../third_party/slang"
    CACHE PATH
    "Path to the pinned Slang source tree"
)

if(NOT EXISTS "${SLANG_SLIM_SLANG_SOURCE_DIR}/CMakeLists.txt")
    message(
        FATAL_ERROR
        "Slang source was not found at ${SLANG_SLIM_SLANG_SOURCE_DIR}. "
        "Initialize submodules with: git submodule update --init --recursive"
    )
endif()

include("${CMAKE_CURRENT_LIST_DIR}/cmake/SlangSlimOptions.cmake")
slang_slim_configure_slang()

add_subdirectory(
    "${SLANG_SLIM_SLANG_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}/slang"
)

if(NOT TARGET slang)
    message(FATAL_ERROR "The pinned Slang source did not define the slang target")
endif()

add_library(
    slang-slim-c-api
    STATIC
    "${CMAKE_CURRENT_LIST_DIR}/src/slang_slim.cpp"
)
target_compile_features(slang-slim-c-api PRIVATE cxx_std_17)
target_compile_definitions(slang-slim-c-api PRIVATE SLANG_SLIM_BUILDING)
target_include_directories(
    slang-slim-c-api
    PUBLIC
        "${CMAKE_CURRENT_LIST_DIR}/include"
)
target_link_libraries(slang-slim-c-api PUBLIC slang)

# This stable meta-target is the native dependency consumed by the future
# sys crate packaging step.
add_custom_target(slang-slim-native DEPENDS slang-slim-c-api)

# This executable is a maintainer-only feasibility probe. It is deliberately
# excluded from normal builds and is not installed or distributed.
add_executable(
    slang-slim-feasibility
    EXCLUDE_FROM_ALL
    "${CMAKE_CURRENT_LIST_DIR}/feasibility/main.cpp"
)
target_link_libraries(slang-slim-feasibility PRIVATE slang)

# A small executable exercises the public C ABI without depending on any
# internal Slang headers. It is excluded from normal builds and is not
# installed or distributed.
add_executable(
    slang-slim-abi-feasibility
    EXCLUDE_FROM_ALL
    "${CMAKE_CURRENT_LIST_DIR}/feasibility/abi_main.cpp"
)
target_link_libraries(slang-slim-abi-feasibility PRIVATE slang-slim-c-api)
