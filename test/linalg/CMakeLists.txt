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
    # Extension compatibility requires actual base mdspan and the backport.
    if(FORGE_FORCE_LINALG_BACKPORT
            OR (NOT FORGE_LINALG_FULL AND NOT FORGE_LINALG_PARTIAL))
        forge_add_linalg_compile_probe(
            test_linalg_extension_compat
            test_linalg_extension_compat.cpp
            linalg_extension_compat_compile)
        add_test(NAME linalg_extension_compat COMMAND test_linalg_extension_compat)
        set_tests_properties(linalg_extension_compat PROPERTIES
            DEPENDS linalg_extension_compat_compile
            TIMEOUT ${FORGE_TEST_TIMEOUT_SECONDS})
        if(MSVC)
            target_compile_options(test_linalg_extension_compat PRIVATE /we4996)
        elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(test_linalg_extension_compat PRIVATE -Werror=deprecated-declarations)
        endif()
    endif()
    forge_add_linalg_runtime_test(test_linalg_auxiliary test_linalg_auxiliary.cpp linalg_auxiliary)
    forge_add_linalg_runtime_test(test_linalg_level1 test_linalg_level1.cpp linalg_level1)
    forge_add_linalg_runtime_test(test_linalg_adl_helpers test_linalg_adl_helpers.cpp linalg_adl_helpers)
    forge_add_linalg_runtime_test(test_linalg_scale_ordering test_linalg_scale_ordering.cpp linalg_scale_ordering)
    forge_add_linalg_runtime_test(test_linalg_unique_outputs test_linalg_unique_outputs.cpp linalg_unique_outputs)
    forge_add_linalg_runtime_test(test_linalg_transpose_span_size test_linalg_transpose_span_size.cpp linalg_transpose_span_size)
    forge_add_linalg_runtime_test(test_linalg_dot_mixed_sum test_linalg_dot_mixed_sum.cpp linalg_dot_mixed_sum)
    forge_add_linalg_runtime_test(test_linalg_level2 test_linalg_level2.cpp linalg_level2)
    forge_add_linalg_runtime_test(test_linalg_level3 test_linalg_level3.cpp linalg_level3)
    forge_add_linalg_runtime_test(test_linalg_product_layouts test_linalg_product_layouts.cpp linalg_product_layouts)
    # The scalar references and strip kernels intentionally compare FP bits.
    if(MSVC)
        target_compile_options(test_linalg_product_layouts PRIVATE /fp:strict)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang|Zig)$")
        target_compile_options(test_linalg_product_layouts PRIVATE -fno-fast-math -ffp-contract=off)
    endif()
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
    forge_add_linalg_compile_probe(
        test_linalg_algorithm_tags
        test_linalg_algorithm_tags.cpp
        linalg_algorithm_tags_compile)
    forge_add_linalg_compile_probe(
        test_linalg_output_uniqueness
        test_linalg_output_uniqueness.cpp
        linalg_output_uniqueness_compile)
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

if(FORGE_LINALG_HAS_MDSPAN_HEADER AND (FORGE_FORCE_LINALG_BACKPORT OR (NOT FORGE_LINALG_FULL AND NOT FORGE_LINALG_PARTIAL)))

# Deprecated extension calls remain usable and diagnose independently.
set(_forge_extension_flags "${CMAKE_REQUIRED_FLAGS}")
set(_forge_extension_defs "${CMAKE_REQUIRED_DEFINITIONS}")
if(MSVC)
    string(APPEND CMAKE_REQUIRED_FLAGS " /we4996")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    string(APPEND CMAKE_REQUIRED_FLAGS " -Werror=deprecated-declarations")
endif()
forge_add_linalg_compile_probe(test_linalg_integer_compat test_linalg_integer_compat.cpp linalg_integer_compat_compile)
add_test(NAME linalg_integer_compat COMMAND test_linalg_integer_compat)
set_tests_properties(linalg_integer_compat PROPERTIES DEPENDS linalg_integer_compat_compile)
if(MSVC)
    target_compile_options(test_linalg_integer_compat PRIVATE /we4996)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_linalg_integer_compat PRIVATE -Werror=deprecated-declarations)
endif()
if(MSVC OR CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    forge_expect_linalg_cxx_source_compiles(FORGE_LINALG_INTEGER_COMPAT
        test_linalg_integer_compat.cpp ON "Suppressed legacy extensions and Forge replacements must compile")
    foreach(_forge_case RANGE 1 12)
        set(CMAKE_REQUIRED_DEFINITIONS ${_forge_extension_defs} -DFORGE_DEPRECATION_CASE=${_forge_case})
        forge_expect_linalg_cxx_source_compiles(FORGE_LINALG_INTEGER_DEPRECATED_${_forge_case}
            test_linalg_integer_compat.cpp OFF "Each legacy extension must diagnose deprecation")
    endforeach()
    set(CMAKE_REQUIRED_DEFINITIONS "${_forge_extension_defs}")
endif()
set(CMAKE_REQUIRED_FLAGS "${_forge_extension_flags}")
set(CMAKE_REQUIRED_DEFINITIONS "${_forge_extension_defs}")
unset(_forge_extension_flags)
unset(_forge_extension_defs)
unset(_forge_case)
endif()

if(TARGET test_linalg_integer_compat)
    forge_add_deprecation_strict_copy(test_linalg_level1 linalg_level1)
    add_custom_target(test_linalg_extension_deprecations_strict DEPENDS
        test_linalg_level1_deprecation_strict test_linalg_integer_compat test_linalg_extension_compat)
endif()
