set(FORGE_SUBMDSPAN_TEST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(FORGE_SUBMDSPAN_RUNTIME_DIR "${FORGE_SUBMDSPAN_TEST_ROOT}/runtime")
set(FORGE_SUBMDSPAN_COMPILE_DIR "${FORGE_SUBMDSPAN_TEST_ROOT}/compile")

function(forge_expect_submdspan_cxx_source_compiles result_var source_name should_compile failure_message)
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
        "${FORGE_SUBMDSPAN_COMPILE_DIR}/${source_name}"
        ${should_compile}
        "${failure_message}")
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

    forge_add_compile_probe(
        test_submdspan_accept_padded_conversions
        "${FORGE_SUBMDSPAN_COMPILE_DIR}/test_submdspan_accept_padded_conversions.cpp"
        submdspan_accept_padded_conversions_compile)

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
else()
    message(STATUS "Forge: submdspan tests skipped (toolchain has no <mdspan>)")
endif()
