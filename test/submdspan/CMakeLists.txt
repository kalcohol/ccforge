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
