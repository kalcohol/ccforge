# Native handoff: all std-injecting wrappers in one TU. Guards against
# ODR/redefinition regressions when a toolchain ships (partial) native support
# and Forge is expected to stand aside.
forge_add_runtime_test(native_handoff_test native_handoff_test.cpp NativeHandoff)

add_test(
    NAME native_handoff_partial_simd_configure
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/partial_simd_project
        -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/partial_simd_project
        -DFORGE_ROOT_DIR=${CMAKE_SOURCE_DIR}
        -DCXX_STANDARD=${CMAKE_CXX_STANDARD}
        "-DGENERATOR=${CMAKE_GENERATOR}"
        "-DCXX_COMPILER=${CMAKE_CXX_COMPILER}"
        "-DMAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/verify_partial_simd.cmake)
set_tests_properties(native_handoff_partial_simd_configure PROPERTIES
    TIMEOUT ${FORGE_TEST_TIMEOUT_SECONDS})
