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

function(forge_expect_linalg_cxx_source_compiles result_var source_name should_compile failure_message)
    set(_FORGE_LINALG_SAVED_CMAKE_REQUIRED_INCLUDES "${CMAKE_REQUIRED_INCLUDES}")
    set(_FORGE_LINALG_SAVED_CMAKE_REQUIRED_DEFINITIONS "${CMAKE_REQUIRED_DEFINITIONS}")
    set(CMAKE_REQUIRED_INCLUDES
        "${CMAKE_SOURCE_DIR}/backport"
        "${CMAKE_SOURCE_DIR}/include")
    foreach(_forge_required_target IN ITEMS forge_std forge)
        if(TARGET ${_forge_required_target})
            get_target_property(
                _forge_target_definitions
                ${_forge_required_target}
                INTERFACE_COMPILE_DEFINITIONS)
            if(_forge_target_definitions)
                foreach(_forge_definition IN LISTS _forge_target_definitions)
                    list(APPEND CMAKE_REQUIRED_DEFINITIONS "-D${_forge_definition}")
                endforeach()
            endif()
        endif()
    endforeach()
    forge_expect_cxx_source_compiles(
        ${result_var}
        "${FORGE_LINALG_COMPILE_DIR}/${source_name}"
        ${should_compile}
        "${failure_message}")
    set(CMAKE_REQUIRED_INCLUDES "${_FORGE_LINALG_SAVED_CMAKE_REQUIRED_INCLUDES}")
    set(CMAKE_REQUIRED_DEFINITIONS "${_FORGE_LINALG_SAVED_CMAKE_REQUIRED_DEFINITIONS}")
    unset(_forge_target_definitions)
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
    forge_add_linalg_compile_probe(
        test_linalg_packed_contracts
        test_linalg_packed_contracts.cpp
        linalg_packed_contracts_compile)
    forge_expect_linalg_cxx_source_compiles(
        FORGE_LINALG_REJECTS_INVALID_PACKED_TAGS
        test_linalg_reject_invalid_packed_tags.cpp
        OFF
        "layout_blas_packed must reject unsupported triangle tags")
    forge_expect_linalg_cxx_source_compiles(
        FORGE_LINALG_REJECTS_INVALID_PACKED_STORAGE
        test_linalg_reject_invalid_packed_storage.cpp
        OFF
        "layout_blas_packed must reject unsupported storage-order tags")
    forge_expect_linalg_cxx_source_compiles(
        FORGE_LINALG_REJECTS_NONSQUARE_PACKED_EXTENTS
        test_linalg_reject_nonsquare_packed_extents.cpp
        OFF
        "layout_blas_packed must reject statically nonsquare extents")
    forge_expect_linalg_cxx_source_compiles(
        FORGE_LINALG_REJECTS_UNREPRESENTABLE_PACKED_SIZE
        test_linalg_reject_unrepresentable_packed_size.cpp
        OFF
        "layout_blas_packed must reject unrepresentable static packed sizes")
else()
    message(STATUS "Forge: linalg tests skipped (toolchain has no <mdspan>)")
endif()
