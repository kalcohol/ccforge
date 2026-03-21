set(FORGE_UNIQUE_RESOURCE_TEST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(FORGE_UNIQUE_RESOURCE_RUNTIME_DIR "${FORGE_UNIQUE_RESOURCE_TEST_ROOT}/runtime")
set(FORGE_UNIQUE_RESOURCE_COMPILE_DIR "${FORGE_UNIQUE_RESOURCE_TEST_ROOT}/compile")

forge_add_runtime_test(
    test_unique_resource
    "${FORGE_UNIQUE_RESOURCE_RUNTIME_DIR}/test_unique_resource.cpp"
    unique_resource_runtime)
forge_add_runtime_test(
    test_unique_resource_checked
    "${FORGE_UNIQUE_RESOURCE_RUNTIME_DIR}/test_unique_resource_checked.cpp"
    unique_resource_checked)
forge_add_runtime_test(
    test_unique_resource_exception
    "${FORGE_UNIQUE_RESOURCE_RUNTIME_DIR}/test_unique_resource_exception.cpp"
    unique_resource_exception)
forge_add_runtime_test(
    test_unique_resource_wording
    "${FORGE_UNIQUE_RESOURCE_RUNTIME_DIR}/test_unique_resource_wording.cpp"
    unique_resource_wording)

forge_add_compile_probe(
    test_unique_resource_api
    "${FORGE_UNIQUE_RESOURCE_COMPILE_DIR}/test_unique_resource_api.cpp"
    unique_resource_api_compile)
forge_add_compile_probe(
    test_unique_resource_checked_noexcept
    "${FORGE_UNIQUE_RESOURCE_COMPILE_DIR}/test_unique_resource_checked_noexcept.cpp"
    unique_resource_checked_noexcept_compile)
forge_add_compile_probe(
    test_unique_resource_include
    "${FORGE_UNIQUE_RESOURCE_COMPILE_DIR}/test_unique_resource_include.cpp"
    unique_resource_include_compile)
