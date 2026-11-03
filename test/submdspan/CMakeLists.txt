set(FORGE_SUBMDSPAN_TEST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(FORGE_SUBMDSPAN_RUNTIME_DIR "${FORGE_SUBMDSPAN_TEST_ROOT}/runtime")
set(FORGE_SUBMDSPAN_COMPILE_DIR "${FORGE_SUBMDSPAN_TEST_ROOT}/compile")

function(forge_check_submdspan_cxx_source_compiles result_var source_file)
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
    if(ARGC GREATER 2)
        forge_expect_cxx_source_compiles(
            ${result_var} "${source_file}" ${ARGV2} "${ARGV3}")
        get_property(_forge_probe_result CACHE ${result_var} PROPERTY VALUE)
    else()
        if(MSVC AND NOT CMAKE_REQUIRED_FLAGS MATCHES "(^| )/Zc:__cplusplus($| )")
            set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} /Zc:__cplusplus")
        endif()
        forge_detect_cxx_source_compiles(${result_var} "${source_file}")
        set(_forge_probe_result "${${result_var}}")
    endif()
    set(${result_var} "${_forge_probe_result}" PARENT_SCOPE)
endfunction()

function(forge_expect_submdspan_cxx_source_compiles result_var source_name should_compile failure_message)
    forge_check_submdspan_cxx_source_compiles(
        ${result_var}
        "${FORGE_SUBMDSPAN_COMPILE_DIR}/${source_name}"
        ${should_compile}
        "${failure_message}")
    set(${result_var} "${${result_var}}" PARENT_SCOPE)
endfunction()

function(forge_expect_submdspan_positive_control result_var source_name failure_message)
    list(APPEND CMAKE_REQUIRED_DEFINITIONS -DFORGE_SUBMDSPAN_POSITIVE_CONTROL=1)
    forge_expect_submdspan_cxx_source_compiles(
        ${result_var} "${source_name}" ON "${failure_message}")
endfunction()

# Runtime tests require <mdspan> to be available.
# On toolchains without <mdspan> (e.g. GCC 13 libstdc++) the test binary
# will simply not be buildable; CMake skips it gracefully via the guard below.
include(CheckCXXSourceCompiles)
unset(FORGE_SUBMDSPAN_HAS_MDSPAN_HEADER CACHE)
unset(FORGE_SUBMDSPAN_HAS_MDSPAN_HEADER)
check_cxx_source_compiles("
    #include <mdspan>
    int main() { return 0; }
" FORGE_SUBMDSPAN_HAS_MDSPAN_HEADER)

if(FORGE_SUBMDSPAN_HAS_MDSPAN_HEADER)
    set(_forge_submdspan_backport_tests OFF)
    if(FORGE_FORCE_SUBMDSPAN_BACKPORT
            OR (NOT FORGE_SUBMDSPAN_FULL AND NOT FORGE_SUBMDSPAN_PARTIAL))
        set(_forge_submdspan_backport_tests ON)
    endif()
    set(_forge_padded_backport_tests OFF)
    if(FORGE_FORCE_MDSPAN_PADDED_LAYOUTS_BACKPORT
            OR (NOT FORGE_MDSPAN_PADDED_LAYOUTS_FULL
                AND NOT FORGE_MDSPAN_PADDED_LAYOUTS_PARTIAL))
        set(_forge_padded_backport_tests ON)
    endif()
    set(_forge_constant_wrapper_backport_tests OFF)
    if(FORGE_FORCE_CONSTANT_WRAPPER_BACKPORT
            OR (NOT FORGE_CONSTANT_WRAPPER_FULL AND NOT FORGE_CONSTANT_WRAPPER_PARTIAL))
        set(_forge_constant_wrapper_backport_tests ON)
    endif()

    # These sources assert Forge-specific overloads and depend on all three backports.
    if(_forge_submdspan_backport_tests
            AND _forge_padded_backport_tests
            AND _forge_constant_wrapper_backport_tests)
        forge_add_runtime_test(
            test_submdspan
            "${FORGE_SUBMDSPAN_RUNTIME_DIR}/test_submdspan.cpp"
            submdspan_runtime)

        forge_add_compile_probe(
            test_submdspan_api
            "${FORGE_SUBMDSPAN_COMPILE_DIR}/test_submdspan_api.cpp"
            submdspan_api_compile)

        forge_add_compile_probe(
            test_submdspan_include
            "${FORGE_SUBMDSPAN_COMPILE_DIR}/test_submdspan_include.cpp"
            submdspan_include_compile)
    else()
        message(STATUS
            "Forge: submdspan implementation tests skipped because a native dependency stands aside")
    endif()

    forge_add_compile_probe(
        test_submdspan_portable_include
        "${FORGE_SUBMDSPAN_COMPILE_DIR}/test_submdspan_portable_include.cpp"
        submdspan_portable_include_compile)

    set(_forge_submdspan_portable_tests ON)
    if(FORGE_SUBMDSPAN_PARTIAL
            AND NOT FORGE_SUBMDSPAN_FULL
            AND NOT _forge_submdspan_backport_tests)
        forge_check_submdspan_cxx_source_compiles(
            FORGE_SUBMDSPAN_PARTIAL_HAS_PORTABLE_OPERATIONS
            "${FORGE_SUBMDSPAN_RUNTIME_DIR}/test_submdspan_portable.cpp")
        if(NOT FORGE_SUBMDSPAN_PARTIAL_HAS_PORTABLE_OPERATIONS)
            set(_forge_submdspan_portable_tests OFF)
            message(STATUS
                "Forge: PARTIAL native submdspan lacks portable operations; wrapper include validation remains enabled")
        endif()
    endif()
    if(_forge_submdspan_portable_tests)
        set(_forge_submdspan_portable_cases portable)
        if((_forge_padded_backport_tests OR FORGE_MDSPAN_PADDED_LAYOUTS_FULL)
                AND (_forge_submdspan_backport_tests OR FORGE_SUBMDSPAN_FULL))
            list(APPEND _forge_submdspan_portable_cases portable_padded)
        endif()
        foreach(_forge_portable_case IN LISTS _forge_submdspan_portable_cases)
            add_executable(
                test_submdspan_${_forge_portable_case}
                "${FORGE_SUBMDSPAN_RUNTIME_DIR}/test_submdspan_portable.cpp")
            target_link_libraries(test_submdspan_${_forge_portable_case} PRIVATE forge::forge)
            if(_forge_portable_case STREQUAL "portable_padded")
                target_compile_definitions(test_submdspan_${_forge_portable_case} PRIVATE
                    FORGE_TEST_SUBMDSPAN_PADDED=1)
            endif()
            add_test(NAME submdspan_${_forge_portable_case}_runtime
                COMMAND test_submdspan_${_forge_portable_case})
            set_tests_properties(submdspan_${_forge_portable_case}_runtime PROPERTIES
                TIMEOUT ${FORGE_TEST_TIMEOUT_SECONDS})
        endforeach()
        unset(_forge_portable_case)
        unset(_forge_submdspan_portable_cases)
    endif()

    if(_forge_padded_backport_tests OR FORGE_MDSPAN_PADDED_LAYOUTS_FULL)
        forge_add_compile_probe(
            test_submdspan_accept_padded_conversions
            "${FORGE_SUBMDSPAN_COMPILE_DIR}/test_submdspan_accept_padded_conversions.cpp"
            submdspan_accept_padded_conversions_compile)
    else()
        message(STATUS
            "Forge: padded conversion compile test skipped for PARTIAL native padded layouts")
    endif()

    if(NOT FORGE_MDSPAN_PADDED_LAYOUTS_FULL
            AND NOT FORGE_MDSPAN_PADDED_LAYOUTS_PARTIAL)
        forge_expect_submdspan_cxx_source_compiles(
            FORGE_SUBMDSPAN_ACCEPTS_PADDED_CONVERSIONS
            test_submdspan_accept_padded_conversions.cpp
            ON
            "padded conversion probes require compatible conversions to compile")
        foreach(_forge_case IN ITEMS left_padded_padding right_padded_padding)
            string(TOUPPER "${_forge_case}" _forge_case_upper)
            forge_expect_submdspan_positive_control(
                FORGE_SUBMDSPAN_ACCEPTS_CONTROL_${_forge_case_upper}
                "test_submdspan_reject_${_forge_case}.cpp"
                "padded conversion positive control must compile: ${_forge_case}")
        endforeach()
        unset(_forge_case)
        unset(_forge_case_upper)
        foreach(_forge_padded_conversion IN ITEMS
                left_unpadded_to_padded right_unpadded_to_padded
                left_padded_to_unpadded right_padded_to_unpadded
                left_padded_padding right_padded_padding)
            string(TOUPPER "${_forge_padded_conversion}" _forge_padded_conversion_upper)
            forge_expect_submdspan_cxx_source_compiles(
                FORGE_SUBMDSPAN_REJECTS_${_forge_padded_conversion_upper}
                test_submdspan_reject_${_forge_padded_conversion}.cpp
                OFF
                "padded conversion must reject ${_forge_padded_conversion}")
        endforeach()
        unset(_forge_padded_conversion)
        unset(_forge_padded_conversion_upper)

        forge_expect_submdspan_cxx_source_compiles(
            FORGE_SUBMDSPAN_ACCEPTS_PADDED_STATIC_CONTROLS
            test_mdspan_padded_static_controls.cpp
            ON
            "padded static Mandates must retain rank-zero, rank-one, zero and dynamic controls")
        set(_forge_padded_static_saved_definitions "${CMAKE_REQUIRED_DEFINITIONS}")
        foreach(_forge_padded_static_layout IN ITEMS left right)
            if(_forge_padded_static_layout STREQUAL "right")
                set(_forge_padded_static_right 1)
            else()
                set(_forge_padded_static_right 0)
            endif()
            string(TOUPPER "${_forge_padded_static_layout}" _forge_padded_static_layout_upper)
            foreach(_forge_padded_static_case IN ITEMS
                    stride partial_stride product size_round size_product index_product padding)
                string(TOUPPER "${_forge_padded_static_case}" _forge_padded_static_case_upper)
                foreach(_forge_padded_static_reject IN ITEMS 0 1)
                    if(_forge_padded_static_reject)
                        set(_forge_padded_static_expect OFF)
                        set(_forge_padded_static_result REJECTS)
                    else()
                        set(_forge_padded_static_expect ON)
                        set(_forge_padded_static_result ACCEPTS)
                    endif()
                    set(CMAKE_REQUIRED_DEFINITIONS "${_forge_padded_static_saved_definitions}")
                    list(APPEND CMAKE_REQUIRED_DEFINITIONS
                        "-DFORGE_TEST_PADDED_RIGHT=${_forge_padded_static_right}"
                        "-DFORGE_TEST_PADDED_REJECT=${_forge_padded_static_reject}"
                        "-DFORGE_TEST_PADDED_${_forge_padded_static_case_upper}=1")
                    forge_expect_submdspan_cxx_source_compiles(
                        FORGE_SUBMDSPAN_${_forge_padded_static_result}_PADDED_${_forge_padded_static_layout_upper}_${_forge_padded_static_case_upper}
                        test_mdspan_padded_static_representability.cpp
                        ${_forge_padded_static_expect}
                        "padded ${_forge_padded_static_layout} static ${_forge_padded_static_case}: expected compile=${_forge_padded_static_expect}")
                endforeach()
            endforeach()
        endforeach()
        set(CMAKE_REQUIRED_DEFINITIONS "${_forge_padded_static_saved_definitions}")
        unset(_forge_padded_static_saved_definitions)
        unset(_forge_padded_static_layout)
        unset(_forge_padded_static_layout_upper)
        unset(_forge_padded_static_right)
        unset(_forge_padded_static_case)
        unset(_forge_padded_static_case_upper)
        unset(_forge_padded_static_reject)
        unset(_forge_padded_static_expect)
        unset(_forge_padded_static_result)
    endif()

    if(NOT MSVC
            AND NOT FORGE_MDSPAN_PADDED_LAYOUTS_FULL
            AND NOT FORGE_MDSPAN_PADDED_LAYOUTS_PARTIAL
            AND NOT FORGE_SUBMDSPAN_FULL
            AND NOT FORGE_SUBMDSPAN_PARTIAL)
        set(_forge_native_padded_handoff_root
            "${FORGE_SUBMDSPAN_TEST_ROOT}/native_padded_handoff")
        add_executable(
            test_submdspan_native_padded_handoff
            EXCLUDE_FROM_ALL
            "${_forge_native_padded_handoff_root}/main.cpp")
        target_compile_definitions(
            test_submdspan_native_padded_handoff PRIVATE
            FORGE_HAS_NATIVE_MDSPAN_PADDED_LAYOUTS=1)
        target_include_directories(
            test_submdspan_native_padded_handoff BEFORE PRIVATE
            "${FORGE_BACKPORT_DIR}"
            "${_forge_native_padded_handoff_root}/include")
        add_test(
            NAME submdspan_native_padded_handoff_compile
            COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
                ${FORGE_TEST_BUILD_CONFIG_ARGS}
                --target test_submdspan_native_padded_handoff)
        set_tests_properties(
            submdspan_native_padded_handoff_compile PROPERTIES
            RESOURCE_LOCK forge_main_build
            TIMEOUT ${FORGE_TEST_TIMEOUT_SECONDS})
        unset(_forge_native_padded_handoff_root)
    endif()
    if(_forge_submdspan_backport_tests
            AND _forge_padded_backport_tests
            AND _forge_constant_wrapper_backport_tests)
        forge_expect_submdspan_cxx_source_compiles(
            FORGE_SUBMDSPAN_ACCEPTS_SLICE_MEMBER_TYPES
            test_submdspan_accept_slice_member_types.cpp
            ON
            "slice member Mandates must accept integer and integral-constant-like controls")
        set(_forge_slice_member_saved_definitions "${CMAKE_REQUIRED_DEFINITIONS}")
        foreach(_forge_slice_member_case RANGE 0 18)
            if(_forge_slice_member_case EQUAL 0)
                set(_forge_slice_member_fields 0 1 2 3 4 5)
            else()
                set(_forge_slice_member_fields 0)
            endif()
            foreach(_forge_slice_member_field IN LISTS _forge_slice_member_fields)
                foreach(_forge_slice_member_reject IN ITEMS 0 1)
                    if(_forge_slice_member_reject)
                        set(_forge_slice_member_expect OFF)
                        set(_forge_slice_member_result REJECTS)
                        set(_forge_slice_member_positive 0)
                    else()
                        set(_forge_slice_member_expect ON)
                        set(_forge_slice_member_result ACCEPTS)
                        set(_forge_slice_member_positive 1)
                    endif()
                    set(CMAKE_REQUIRED_DEFINITIONS "${_forge_slice_member_saved_definitions}")
                    list(APPEND CMAKE_REQUIRED_DEFINITIONS
                        "-DFORGE_TEST_SLICE_CASE=${_forge_slice_member_case}"
                        "-DFORGE_TEST_SLICE_FIELD=${_forge_slice_member_field}"
                        "-DFORGE_TEST_SLICE_POSITIVE=${_forge_slice_member_positive}")
                    forge_expect_submdspan_cxx_source_compiles(
                        FORGE_SUBMDSPAN_${_forge_slice_member_result}_SLICE_MEMBER_${_forge_slice_member_case}_${_forge_slice_member_field}
                        test_submdspan_slice_member_mandates.cpp
                        ${_forge_slice_member_expect}
                        "slice member field ${_forge_slice_member_field}, case ${_forge_slice_member_case}: expected compile=${_forge_slice_member_expect}")
                endforeach()
            endforeach()
        endforeach()
        set(CMAKE_REQUIRED_DEFINITIONS "${_forge_slice_member_saved_definitions}")
        unset(_forge_slice_member_saved_definitions)
        unset(_forge_slice_member_case)
        unset(_forge_slice_member_field)
        unset(_forge_slice_member_fields)
        unset(_forge_slice_member_reject)
        unset(_forge_slice_member_expect)
        unset(_forge_slice_member_result)
        unset(_forge_slice_member_positive)

        foreach(_forge_case IN ITEMS
                dynamic_range_zero_constant_stride dynamic_empty_range_zero_constant_stride
                negative_constant_index_unsigned negative_integral_constant_index_unsigned
                overflow_constant_index_signed overflow_character_constant_index
                overflow_custom_constant_index)
            string(TOUPPER "${_forge_case}" _forge_case_upper)
            forge_expect_submdspan_positive_control(
                FORGE_SUBMDSPAN_ACCEPTS_CONTROL_${_forge_case_upper}
                "test_submdspan_reject_${_forge_case}.cpp"
                "submdspan positive control must compile: ${_forge_case}")
        endforeach()
        unset(_forge_case)
        unset(_forge_case_upper)

        foreach(_forge_zero_stride_case IN ITEMS
                dynamic_range dynamic_empty_range)
            forge_expect_submdspan_cxx_source_compiles(
                FORGE_SUBMDSPAN_REJECTS_${_forge_zero_stride_case}_ZERO_CONSTANT_STRIDE
                "test_submdspan_reject_${_forge_zero_stride_case}_zero_constant_stride.cpp"
                OFF
                "submdspan canonical range must reject a zero constant stride for dynamic spans")
        endforeach()
        unset(_forge_zero_stride_case)
        foreach(_forge_constant_index_case IN ITEMS
                negative_constant_index_unsigned negative_integral_constant_index_unsigned
                overflow_constant_index_signed underflow_constant_index_signed
                overflow_character_constant_index overflow_custom_constant_index)
            string(TOUPPER "${_forge_constant_index_case}" _forge_constant_index_case_upper)
            forge_expect_submdspan_cxx_source_compiles(
                FORGE_SUBMDSPAN_REJECTS_${_forge_constant_index_case_upper}
                "test_submdspan_reject_${_forge_constant_index_case}.cpp"
                OFF
                "submdspan canonical index must reject unrepresentable constants: ${_forge_constant_index_case}")
        endforeach()
        unset(_forge_constant_index_case)
        unset(_forge_constant_index_case_upper)
    endif()
    unset(_forge_submdspan_backport_tests)
    unset(_forge_padded_backport_tests)
    unset(_forge_constant_wrapper_backport_tests)
    unset(_forge_submdspan_portable_tests)
else()
    message(STATUS "Forge: submdspan tests skipped (toolchain has no <mdspan>)")
endif()
