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

# The wrapper library is deliberately deferred until the C ABI step. This
# stable meta-target gives local builds and CI one platform-independent target
# for the compiler dependency in the meantime.
add_custom_target(slang-slim-native DEPENDS slang)
