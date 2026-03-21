set(FORGE_SUBMDSPAN_TEST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(FORGE_SUBMDSPAN_RUNTIME_DIR "${FORGE_SUBMDSPAN_TEST_ROOT}/runtime")
set(FORGE_SUBMDSPAN_COMPILE_DIR "${FORGE_SUBMDSPAN_TEST_ROOT}/compile")

# Runtime tests require <mdspan> to be available.
# On toolchains without <mdspan> (e.g. GCC 13 libstdc++) the test binary
# will simply not be buildable; CMake skips it gracefully via the guard below.
include(CheckCXXSourceCompiles)
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
else()
    message(STATUS "Forge: submdspan tests skipped (toolchain has no <mdspan>)")
endif()
