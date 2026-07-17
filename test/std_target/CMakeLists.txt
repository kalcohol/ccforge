add_executable(test_std_target_compile EXCLUDE_FROM_ALL compile/test_std_target.cpp)
target_link_libraries(test_std_target_compile PRIVATE forge::std)

add_test(
    NAME std_target_compile
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} ${FORGE_TEST_BUILD_CONFIG_ARGS} --target test_std_target_compile)
set_tests_properties(std_target_compile PROPERTIES
    RESOURCE_LOCK forge_main_build
    TIMEOUT ${FORGE_TEST_TIMEOUT_SECONDS})
