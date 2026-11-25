set(FORGE_CONSTANT_WRAPPER_TEST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(FORGE_CONSTANT_WRAPPER_COMPILE_DIR "${FORGE_CONSTANT_WRAPPER_TEST_ROOT}/compile")
set(FORGE_CONSTANT_WRAPPER_RUNTIME_DIR "${FORGE_CONSTANT_WRAPPER_TEST_ROOT}/runtime")

forge_add_runtime_test(
    test_constant_wrapper
    "${FORGE_CONSTANT_WRAPPER_RUNTIME_DIR}/test_constant_wrapper.cpp"
    constant_wrapper_runtime)

forge_add_compile_probe(
    test_constant_wrapper_include
    "${FORGE_CONSTANT_WRAPPER_COMPILE_DIR}/test_constant_wrapper_include.cpp"
    constant_wrapper_include_compile)

forge_add_compile_probe(
    test_constant_wrapper_array_member
    "${FORGE_CONSTANT_WRAPPER_COMPILE_DIR}/test_constant_wrapper_array_member.cpp"
    constant_wrapper_array_member_compile)

set(CMAKE_REQUIRED_INCLUDES "")
forge_expect_cxx_source_compiles(
    FORGE_CONSTANT_WRAPPER_ACCEPTS_MATCHING_TYPE
    "${FORGE_CONSTANT_WRAPPER_COMPILE_DIR}/test_constant_wrapper_accept_matching_type.cpp"
    ON
    "std::constant_wrapper must accept an explicit second template argument matching decltype(X)")
forge_expect_cxx_source_compiles(
    FORGE_CONSTANT_WRAPPER_REJECTS_MISMATCHED_TYPE
    "${FORGE_CONSTANT_WRAPPER_COMPILE_DIR}/test_constant_wrapper_reject_mismatched_type.cpp"
    OFF
    "std::constant_wrapper must reject an explicit second template argument that differs from decltype(X)")
