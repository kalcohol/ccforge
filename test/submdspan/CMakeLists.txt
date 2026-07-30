set(FORGE_SUBMDSPAN_TEST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(FORGE_SUBMDSPAN_RUNTIME_DIR "${FORGE_SUBMDSPAN_TEST_ROOT}/runtime")
set(FORGE_SUBMDSPAN_COMPILE_DIR "${FORGE_SUBMDSPAN_TEST_ROOT}/compile")

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
