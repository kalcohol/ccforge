set(FORGE_LINALG_TEST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(FORGE_LINALG_RUNTIME_DIR "${FORGE_LINALG_TEST_ROOT}/runtime")
set(FORGE_LINALG_COMPILE_DIR "${FORGE_LINALG_TEST_ROOT}/compile")

function(forge_add_linalg_runtime_test target_name source_name test_name)
    forge_add_runtime_test(
        ${target_name}
        "${FORGE_LINALG_RUNTIME_DIR}/${source_name}"
        ${test_name})
    target_include_directories(${target_name} BEFORE PRIVATE "${CMAKE_SOURCE_DIR}/backport")
endfunction()

function(forge_add_linalg_compile_probe target_name source_name test_name)
    forge_add_compile_probe(
        ${target_name}
        "${FORGE_LINALG_COMPILE_DIR}/${source_name}"
        ${test_name})
    target_include_directories(${target_name} BEFORE PRIVATE "${CMAKE_SOURCE_DIR}/backport")
endfunction()

include(CheckCXXSourceCompiles)
unset(FORGE_LINALG_HAS_MDSPAN_HEADER CACHE)
unset(FORGE_LINALG_HAS_MDSPAN_HEADER)
check_cxx_source_compiles("
    #include <mdspan>
    int main() { return 0; }
" FORGE_LINALG_HAS_MDSPAN_HEADER)

if(FORGE_LINALG_HAS_MDSPAN_HEADER)
    forge_add_linalg_runtime_test(test_linalg_auxiliary test_linalg_auxiliary.cpp linalg_auxiliary)
    forge_add_linalg_runtime_test(test_linalg_level1 test_linalg_level1.cpp linalg_level1)
    forge_add_linalg_runtime_test(test_linalg_level2 test_linalg_level2.cpp linalg_level2)
    forge_add_linalg_runtime_test(test_linalg_level3 test_linalg_level3.cpp linalg_level3)
    forge_add_linalg_compile_probe(
        test_linalg_baseline_api
        test_linalg_baseline_api.cpp
        linalg_baseline_api_compile)
    forge_add_linalg_compile_probe(
        test_linalg_feature_macro
        test_linalg_feature_macro.cpp
        linalg_feature_macro_compile)
    forge_add_linalg_compile_probe(
        test_linalg_include
        test_linalg_include.cpp
        linalg_include_compile)
    forge_add_linalg_compile_probe(
        test_linalg_static_extents
        test_linalg_static_extents.cpp
        linalg_static_extents_compile)
    forge_add_linalg_compile_probe(
        test_linalg_static_level23_extents
        test_linalg_static_level23_extents.cpp
        linalg_static_level23_extents_compile)
else()
    message(STATUS "Forge: linalg tests skipped (toolchain has no <mdspan>)")
endif()
