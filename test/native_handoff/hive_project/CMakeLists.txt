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
if(EXPECT_STATE STREQUAL "BACKPORT" OR EXPECT_STATE STREQUAL "FORCED")
    if(NOT TARGET hive_runtime OR NOT TARGET hive_multi_tu
            OR NOT TARGET hive_compile_include
            OR NOT hive_test_directory IN_LIST test_subdirectories)
        message(FATAL_ERROR "main test tree did not admit Hive backport tests")
    endif()
    get_property(hive_registered_tests DIRECTORY "${hive_test_directory}" PROPERTY TESTS)
    foreach(required_test hive_runtime hive_multi_tu hive_compile_include
            hive_model_positive hive_model_mutation)
        if(NOT required_test IN_LIST hive_registered_tests)
            message(FATAL_ERROR "main test tree did not register ${required_test}")
        endif()
    endforeach()
elseif(TARGET hive_runtime OR TARGET hive_multi_tu OR TARGET hive_compile_include
        OR hive_test_directory IN_LIST test_subdirectories)
    message(FATAL_ERROR "main test tree admitted Hive conformance tests on native roots")
endif()

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
