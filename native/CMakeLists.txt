cmake_minimum_required(VERSION 3.22)

project(slang_slim LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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

# Native targets are deliberately added in the next reviewed step. This file
# currently verifies the toolchain and the pinned Slang source location only.

