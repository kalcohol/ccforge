set(FORGE_LINALG_TEST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(FORGE_LINALG_RUNTIME_DIR "${FORGE_LINALG_TEST_ROOT}/runtime")

function(forge_add_linalg_runtime_test target_name source_name test_name)
    forge_add_runtime_test(
        ${target_name}
        "${FORGE_LINALG_RUNTIME_DIR}/${source_name}"
        ${test_name})
    target_include_directories(${target_name} BEFORE PRIVATE "${CMAKE_SOURCE_DIR}/backport")
endfunction()

include(CheckCXXSourceCompiles)
check_cxx_source_compiles("
    #include <mdspan>
    int main() { return 0; }
" FORGE_LINALG_HAS_MDSPAN_HEADER)

if(FORGE_LINALG_HAS_MDSPAN_HEADER)
    forge_add_linalg_runtime_test(test_linalg_core test_linalg_core.cpp linalg_core)
else()
    message(STATUS "Forge: linalg tests skipped (toolchain has no <mdspan>)")
endif()
