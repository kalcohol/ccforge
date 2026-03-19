# Forge Tests

# Add googletest
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/googletest ${CMAKE_CURRENT_BINARY_DIR}/googletest)

set(FORGE_TEST_BUILD_CONFIG_ARGS)
if(CMAKE_CONFIGURATION_TYPES)
    list(APPEND FORGE_TEST_BUILD_CONFIG_ARGS --config $<CONFIG>)
endif()

# Runtime tests for unique_resource
add_executable(test_unique_resource test_unique_resource.cpp)
target_link_libraries(test_unique_resource PRIVATE gtest_main)
add_test(NAME unique_resource_runtime COMMAND test_unique_resource)

add_executable(test_unique_resource_checked test_unique_resource_checked.cpp)
target_link_libraries(test_unique_resource_checked PRIVATE gtest_main)
add_test(NAME unique_resource_checked COMMAND test_unique_resource_checked)

add_executable(test_unique_resource_exception test_unique_resource_exception.cpp)
target_link_libraries(test_unique_resource_exception PRIVATE gtest_main)
add_test(NAME unique_resource_exception COMMAND test_unique_resource_exception)

add_executable(test_unique_resource_wording test_unique_resource_wording.cpp)
target_link_libraries(test_unique_resource_wording PRIVATE gtest_main)
add_test(NAME unique_resource_wording COMMAND test_unique_resource_wording)

# Compile probes for target semantics
add_executable(test_unique_resource_api EXCLUDE_FROM_ALL test_unique_resource_api.cpp)
target_link_libraries(test_unique_resource_api PRIVATE forge::forge)
add_test(
    NAME unique_resource_api_compile
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} ${FORGE_TEST_BUILD_CONFIG_ARGS} --target test_unique_resource_api)

add_executable(test_unique_resource_checked_noexcept EXCLUDE_FROM_ALL test_unique_resource_checked_noexcept.cpp)
add_test(
    NAME unique_resource_checked_noexcept_compile
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} ${FORGE_TEST_BUILD_CONFIG_ARGS} --target test_unique_resource_checked_noexcept)

add_executable(test_unique_resource_include EXCLUDE_FROM_ALL test_unique_resource_include.cpp)
target_link_libraries(test_unique_resource_include PRIVATE forge::forge)
add_test(
    NAME unique_resource_include_compile
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} ${FORGE_TEST_BUILD_CONFIG_ARGS} --target test_unique_resource_include)

# Runtime tests for simd
add_executable(test_simd test_simd.cpp)
target_link_libraries(test_simd PRIVATE gtest_main forge::forge)
target_compile_definitions(test_simd PRIVATE FORGE_SIMD_ENABLE_CHUNK_CAT_PERMUTE_TESTS)
add_test(NAME simd_runtime COMMAND test_simd)

add_executable(test_simd_abi test_simd_abi.cpp)
target_link_libraries(test_simd_abi PRIVATE gtest_main forge::forge)
add_test(NAME simd_traits COMMAND test_simd_abi)

add_executable(test_simd_mask test_simd_mask.cpp)
target_link_libraries(test_simd_mask PRIVATE gtest_main forge::forge)
add_test(NAME simd_mask COMMAND test_simd_mask)

add_executable(test_simd_memory test_simd_memory.cpp)
target_link_libraries(test_simd_memory PRIVATE gtest_main forge::forge)
target_compile_definitions(test_simd_memory PRIVATE FORGE_SIMD_ENABLE_UNCHECKED_MEMORY_TESTS)
add_test(NAME simd_memory COMMAND test_simd_memory)

add_executable(test_simd_operators test_simd_operators.cpp)
target_link_libraries(test_simd_operators PRIVATE gtest_main forge::forge)
add_test(NAME simd_operators COMMAND test_simd_operators)

add_executable(test_simd_math test_simd_math.cpp)
target_link_libraries(test_simd_math PRIVATE gtest_main forge::forge)
add_test(NAME simd_math COMMAND test_simd_math)

# Compile probes for simd public surface
add_executable(test_simd_api EXCLUDE_FROM_ALL test_simd_api.cpp)
target_link_libraries(test_simd_api PRIVATE forge::forge)
target_compile_definitions(test_simd_api PRIVATE FORGE_SIMD_ENABLE_UNCHECKED_MEMORY_PROBES FORGE_SIMD_ENABLE_CHUNK_CAT_PERMUTE_PROBES)
add_test(
    NAME simd_api_compile
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} ${FORGE_TEST_BUILD_CONFIG_ARGS} --target test_simd_api)

add_executable(test_simd_include EXCLUDE_FROM_ALL test_simd_include.cpp)
target_link_libraries(test_simd_include PRIVATE forge::forge)
add_test(
    NAME simd_include_compile
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} ${FORGE_TEST_BUILD_CONFIG_ARGS} --target test_simd_include)

add_executable(test_simd_feature_macro EXCLUDE_FROM_ALL test_simd_feature_macro.cpp)
target_link_libraries(test_simd_feature_macro PRIVATE forge::forge)
add_test(
    NAME simd_feature_macro_compile
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} ${FORGE_TEST_BUILD_CONFIG_ARGS} --target test_simd_feature_macro)
