set(FORGE_LINALG_TEST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(FORGE_LINALG_RUNTIME_DIR "${FORGE_LINALG_TEST_ROOT}/runtime")
set(FORGE_LINALG_COMPILE_DIR "${FORGE_LINALG_TEST_ROOT}/compile")

function(forge_add_linalg_runtime_test target_name source_name test_name)
    forge_add_runtime_test(
        ${target_name}
        "${FORGE_LINALG_RUNTIME_DIR}/${source_name}"
        ${test_name})
    target_include_directories(${target_name} BEFORE PRIVATE "${CMAKE_SOURCE_DIR}/backport")
    if(FORGE_TEST_TBB_LIBRARY)
        target_link_libraries(${target_name} PRIVATE ${FORGE_TEST_TBB_LIBRARY})
    endif()
endfunction()

function(forge_add_linalg_compile_probe target_name source_name test_name)
    forge_add_compile_probe(
        ${target_name}
        "${FORGE_LINALG_COMPILE_DIR}/${source_name}"
        ${test_name})
    target_include_directories(${target_name} BEFORE PRIVATE "${CMAKE_SOURCE_DIR}/backport")
    if(FORGE_TEST_TBB_LIBRARY)
        target_link_libraries(${target_name} PRIVATE ${FORGE_TEST_TBB_LIBRARY})
    endif()
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
    # Mirror forge.cmake's force/full/partial injection decision. All probes
    # that assume Forge constraints, extensions, markers, or arithmetic paths
    # belong inside this gate, including configure-time negative probes.
    set(_forge_linalg_backport_injected OFF)
    if(FORGE_FORCE_LINALG_BACKPORT
            OR (NOT FORGE_LINALG_FULL AND NOT FORGE_LINALG_PARTIAL))
        set(_forge_linalg_backport_injected ON)
    endif()

    # These probes also apply to native <linalg>. A partial implementation only
    # gets the feature-macro check; the full synopsis is needed by the others.
    forge_add_linalg_compile_probe(
        test_linalg_feature_macro
        test_linalg_feature_macro.cpp
        linalg_feature_macro_compile)
    if(_forge_linalg_backport_injected OR FORGE_LINALG_FULL)
        forge_add_linalg_compile_probe(
            test_linalg_include
            test_linalg_include.cpp
            linalg_include_compile)
        forge_add_linalg_runtime_test(test_linalg_overload_scalars test_linalg_overload_scalars.cpp linalg_overload_scalars)
        if(MSVC)
            target_compile_options(test_linalg_overload_scalars PRIVATE /we4996 /bigobj)
        elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|Zig")
            target_compile_options(test_linalg_overload_scalars PRIVATE -Werror=deprecated-declarations)
        endif()
    endif()

    if(_forge_linalg_backport_injected)
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
        forge_add_linalg_runtime_test(test_linalg_trmm_alloc test_linalg_trmm_alloc.cpp linalg_trmm_alloc)
        forge_add_linalg_runtime_test(test_linalg_missing_overloads test_linalg_missing_overloads.cpp linalg_missing_overloads)
        forge_add_linalg_runtime_test(test_linalg_conformance test_linalg_conformance.cpp linalg_conformance)
        forge_add_linalg_compile_probe(test_linalg_conformance_compile test_linalg_conformance.cpp linalg_conformance_compile)
        forge_add_linalg_compile_probe(test_linalg_policy_include_order test_linalg_policy_before.cpp linalg_policy_include_order_compile)
        target_sources(test_linalg_policy_include_order PRIVATE
            "${FORGE_LINALG_COMPILE_DIR}/test_linalg_policy_after.cpp")
        add_test(NAME linalg_policy_include_order COMMAND test_linalg_policy_include_order)
        set_tests_properties(linalg_policy_include_order PROPERTIES
            DEPENDS linalg_policy_include_order_compile
            TIMEOUT ${FORGE_TEST_TIMEOUT_SECONDS})
        forge_add_linalg_compile_probe(test_linalg_native_execution_only test_linalg_native_execution_only.cpp linalg_native_execution_only_compile)

        # Check actual native policy objects with the same include root and MSVC
        # native-header definition as consumers. A header/trait alone is insufficient.
        function(forge_linalg_check_policy_objects result extra_flags)
            set(CMAKE_REQUIRED_INCLUDES "${CMAKE_SOURCE_DIR}/backport")
            set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} ${extra_flags}")
            if(FORGE_TEST_TBB_LIBRARY)
                list(APPEND CMAKE_REQUIRED_LIBRARIES ${FORGE_TEST_TBB_LIBRARY})
            endif()
            get_target_property(_defs forge_std INTERFACE_COMPILE_DEFINITIONS)
            if(_defs)
                foreach(_def IN LISTS _defs)
                    list(APPEND CMAKE_REQUIRED_DEFINITIONS "-D${_def}")
                endforeach()
            endif()
            set(_libcpp_requirement "")
            if(extra_flags)
                set(_libcpp_requirement "#ifndef _LIBCPP_VERSION\n#error Requires libc++\n#endif")
            endif()
            unset(${result} CACHE)
            check_cxx_source_compiles("
                #include <forge_native_execution.hpp>
                ${_libcpp_requirement}
                #if !FORGE_NATIVE_EXECUTION_POLICY_OBJECTS
                #error Native policy objects unavailable
                #endif
                int main() {
                    static_assert(std::is_execution_policy_v<decltype(std::execution::sequenced_policy{})>);
                    (void)std::execution::seq;
                    (void)std::execution::par;
                    (void)std::execution::par_unseq;
                }
            " ${result})
            set(${result} "${${result}}" PARENT_SCOPE)
        endfunction()

        function(forge_linalg_policy_target target suffix extra_flags)
            forge_add_linalg_runtime_test(${target} test_linalg_execution_policy.cpp linalg_execution_policy${suffix})
            target_compile_definitions(${target} PRIVATE FORGE_TEST_LINALG_REQUIRE_POLICIES=1)
            if(extra_flags)
                target_compile_options(${target} PRIVATE ${extra_flags})
                target_link_options(${target} PRIVATE ${extra_flags})
                forge_add_linalg_compile_probe(${target}_compile test_linalg_conformance.cpp linalg_policy_selection${suffix}_compile)
                target_compile_options(${target}_compile PRIVATE ${extra_flags})
                target_link_options(${target}_compile PRIVATE ${extra_flags})
                target_compile_definitions(${target}_compile PRIVATE FORGE_TEST_LINALG_REQUIRE_POLICIES=1)
                if(MSVC)
                    target_compile_options(${target}_compile PRIVATE /we4996 /bigobj)
                elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|Zig")
                    target_compile_options(${target}_compile PRIVATE -Werror=deprecated-declarations)
                endif()
            endif()
            if(MSVC)
                target_compile_options(${target} PRIVATE /we4996 /bigobj)
            elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|Zig")
                target_compile_options(${target} PRIVATE -Werror=deprecated-declarations)
            endif()
        endfunction()

        forge_linalg_check_policy_objects(FORGE_LINALG_TEST_HAS_POLICIES "")
        if(FORGE_LINALG_TEST_HAS_POLICIES)
            forge_linalg_policy_target(test_linalg_execution_policy "" "")
        else()
            message(STATUS "Forge: linalg policy runtime tests skipped (native policy objects unavailable)")
            add_test(NAME linalg_execution_policy_unavailable
                COMMAND ${CMAKE_COMMAND} -E echo "SKIP: native execution policy objects unavailable")
            set_tests_properties(linalg_execution_policy_unavailable PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP:")
            if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|Zig")
                forge_linalg_check_policy_objects(FORGE_LINALG_TEST_HAS_EXPERIMENTAL_POLICIES "-fexperimental-library")
                if(FORGE_LINALG_TEST_HAS_EXPERIMENTAL_POLICIES)
                    forge_linalg_policy_target(test_linalg_execution_policy_experimental _experimental "-fexperimental-library")
                else()
                    message(STATUS "Forge: libc++ linalg policy variant unavailable with -fexperimental-library")
                endif()
            endif()
        endif()
        foreach(_target IN ITEMS test_linalg_missing_overloads
                test_linalg_conformance test_linalg_conformance_compile
                test_linalg_policy_include_order test_linalg_native_execution_only)
            if(MSVC)
                target_compile_options(${_target} PRIVATE /we4996 /bigobj)
            elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|Zig")
                target_compile_options(${_target} PRIVATE -Werror=deprecated-declarations)
            endif()
        endforeach()
        # Non-TRMM scalar references and strip kernels intentionally compare FP bits.
        if(MSVC)
            target_compile_options(test_linalg_product_layouts PRIVATE /fp:strict /bigobj)
        elseif(CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang|Zig)$")
            target_compile_options(test_linalg_product_layouts PRIVATE -fno-fast-math -ffp-contract=off)
        endif()
        forge_add_linalg_compile_probe(
            test_linalg_baseline_api
            test_linalg_baseline_api.cpp
            linalg_baseline_api_compile)
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
        message(STATUS "Forge: backport-specific linalg tests/probes skipped (native linalg stand-aside)")
    endif()
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
        test_linalg_level1_ds test_linalg_integer_compat test_linalg_extension_compat)
endif()
