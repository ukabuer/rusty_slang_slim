# This file is loaded by CMAKE_PROJECT_miniz_INCLUDE immediately after
# miniz's project() call and before its CMakeLists appends /Zi.

function(slang_slim_remove_miniz_common_debug_info)
    string(REPLACE "/Zi" "" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}" PARENT_SCOPE)
endfunction()

cmake_language(DEFER CALL slang_slim_remove_miniz_common_debug_info)
