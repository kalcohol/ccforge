cmake_minimum_required(VERSION 3.20)
project(forge_hive_handoff LANGUAGES CXX)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(surface_include "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(CMAKE_REQUIRED_INCLUDES "${surface_include}")
set(CMAKE_REQUIRED_DEFINITIONS "-DHIVE_FIXTURE_CASE=${HIVE_CASE}")
set(FORGE_ENABLE_FORGE_IO OFF CACHE STRING "" FORCE)
include("${FORGE_ROOT_DIR}/forge.cmake")
if(NOT "${FORGE_CMAKE_DIR}" STREQUAL "${HIVE_EXPECT_PROBE_DIR}")
    message(FATAL_ERROR "forge.cmake did not honor the private probe directory")
endif()
string(REPLACE "," ";" EXPECT_ROOTS "${EXPECT_ROOTS}")

if(EXPECT_STATE STREQUAL "COMPLETE")
    if(NOT FORGE_HIVE_FULL)
        message(FATAL_ERROR "synthetic COMPLETE probe failed")
    endif()
elseif(FORGE_HIVE_FULL)
    message(FATAL_ERROR "incomplete fixture incorrectly passed FULL")
endif()
foreach(root MACRO LIMITS TEMPLATE PMR)
    if(root IN_LIST EXPECT_ROOTS)
        if(NOT FORGE_HIVE_PARTIAL_${root})
            message(FATAL_ERROR "missing independent ${root} root")
        endif()
    elseif(FORGE_HIVE_PARTIAL_${root})
        message(FATAL_ERROR "unexpected ${root} root (possibly stale cache)")
    endif()
endforeach()

# Exercise admission through the real main test tree, not a duplicate gate.
# Its unrelated test commands may use CMAKE_SOURCE_DIR, but are only registered
# here: this fixture never executes them or builds their targets.
enable_testing()
foreach(group EXECUTION SIMD CONSTANT_WRAPPER UNIQUE_RESOURCE SUBMDSPAN LINALG
        FORGE FORGE_RUNTIME FORGE_RESOURCE FORGE_IO FORGE_ERASURE NATIVE_HANDOFF)
    set(FORGE_TEST_ENABLE_${group} OFF CACHE BOOL "Hive gate fixture" FORCE)
endforeach()
set(FORGE_TEST_ENABLE_HIVE ON CACHE BOOL "Hive gate fixture" FORCE)
set(FORGE_HIVE_BUILD_COST_PROBE OFF CACHE BOOL "Hive gate fixture" FORCE)
set(FORGE_HIVE_BUILD_STRESS OFF CACHE BOOL "Hive gate fixture" FORCE)
if(MSVC)
    set(gtest_force_shared_crt ON CACHE BOOL "Match the parent test CRT" FORCE)
endif()
add_subdirectory("${FORGE_ROOT_DIR}/test" "${CMAKE_CURRENT_BINARY_DIR}/registered-tests"
    EXCLUDE_FROM_ALL)

get_property(test_subdirectories DIRECTORY "${FORGE_ROOT_DIR}/test" PROPERTY SUBDIRECTORIES)
set(hive_test_directory "${FORGE_ROOT_DIR}/test/hive")
if(NOT hive_test_directory IN_LIST test_subdirectories)
    message(FATAL_ERROR "main test tree did not enter the Hive test directory")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../HiveTestExpectations.cmake")
hive_expected_registrations("${EXPECT_STATE}" expected_targets expected_tests)
# Recurse through the entire registered tree so an implementation target or
# test moved into another directory cannot evade the native-root check.
function(hive_collect_registrations directory targets_var tests_var)
    get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    get_property(tests DIRECTORY "${directory}" PROPERTY TESTS)
    get_property(children DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(child IN LISTS children)
        hive_collect_registrations("${child}" child_targets child_tests)
        list(APPEND targets ${child_targets})
        list(APPEND tests ${child_tests})
    endforeach()
    set(${targets_var} "${targets}" PARENT_SCOPE)
    set(${tests_var} "${tests}" PARENT_SCOPE)
endfunction()
hive_collect_registrations("${FORGE_ROOT_DIR}/test" actual_targets actual_tests)
hive_assert_registrations("main tree Hive targets (${EXPECT_STATE})"
    "${actual_targets}" "${expected_targets}")
hive_assert_registrations("main tree Hive tests (${EXPECT_STATE})"
    "${actual_tests}" "${expected_tests}")

add_executable(hive_consumer main.cpp)
# Deliberately expose the shared backport path even when Hive is native. Other
# backports commonly require this path, so omitting it would miss guard bugs.
target_include_directories(hive_consumer BEFORE PRIVATE
    "${FORGE_BACKPORT_DIR}" "${surface_include}")
target_link_libraries(hive_consumer PRIVATE forge::std)
target_compile_definitions(hive_consumer PRIVATE
    HIVE_FIXTURE_CASE=${HIVE_CASE}
    HIVE_EXPECT_NATIVE=$<BOOL:${FORGE_HIVE_PARTIAL}>)
if(MSVC)
    # Route the wrapper to this synthetic native header, not the host's header.
    get_target_property(definitions forge_std INTERFACE_COMPILE_DEFINITIONS)
    list(FILTER definitions EXCLUDE REGEX "^FORGE_MSVC_HIVE_HEADER=")
    set_property(TARGET forge_std PROPERTY INTERFACE_COMPILE_DEFINITIONS "${definitions}")
    target_compile_definitions(hive_consumer PRIVATE
        FORGE_MSVC_HIVE_HEADER="${surface_include}/hive")
endif()
