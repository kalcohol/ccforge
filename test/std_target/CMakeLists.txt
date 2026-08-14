add_executable(test_std_target_compile compile/test_std_target.cpp)
target_link_libraries(test_std_target_compile PRIVATE forge::std)

add_test(
    NAME std_target_compile
    COMMAND test_std_target_compile)
set_tests_properties(std_target_compile PROPERTIES
    TIMEOUT ${FORGE_TEST_TIMEOUT_SECONDS})
