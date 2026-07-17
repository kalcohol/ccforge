# Forge Tests

option(FORGE_TEST_ENABLE_EXECUTION "Build Forge std::execution tests" ON)
option(FORGE_TEST_ENABLE_SIMD "Build Forge std::simd tests" ON)
option(FORGE_TEST_ENABLE_UNIQUE_RESOURCE "Build Forge std::unique_resource tests" ON)
option(FORGE_TEST_ENABLE_SUBMDSPAN "Build Forge std::submdspan tests" ON)
option(FORGE_TEST_ENABLE_LINALG "Build Forge std::linalg tests" ON)
option(FORGE_TEST_ENABLE_FORGE "Build Forge extension tests" ON)
option(FORGE_TEST_ENABLE_FORGE_RUNTIME "Build Forge runtime extension tests" ON)
option(FORGE_TEST_ENABLE_FORGE_RESOURCE "Build Forge resource policy extension tests" ON)
option(FORGE_TEST_ENABLE_FORGE_IO "Build Forge IO extension tests when available" ON)
option(FORGE_TEST_ENABLE_FORGE_ERASURE "Build Forge erasure extension tests" ON)
option(FORGE_TEST_ENABLE_NATIVE_HANDOFF "Build Forge native handoff tests" ON)

set(FORGE_TEST_ENABLE_ANY_FORGE OFF)
if(FORGE_TEST_ENABLE_FORGE
        AND (FORGE_TEST_ENABLE_FORGE_RUNTIME
            OR (FORGE_ENABLE_FORGE_RESOURCE_POLICY
                AND FORGE_TEST_ENABLE_FORGE_RESOURCE)
            OR FORGE_TEST_ENABLE_FORGE_IO
            OR (FORGE_HAS_FORGE_IO_BACKEND
                AND FORGE_TEST_ENABLE_FORGE_IO)
            OR FORGE_TEST_ENABLE_FORGE_ERASURE))
    set(FORGE_TEST_ENABLE_ANY_FORGE ON)
endif()

if(FORGE_TEST_ENABLE_EXECUTION
        OR FORGE_TEST_ENABLE_SIMD
        OR FORGE_TEST_ENABLE_UNIQUE_RESOURCE
        OR FORGE_TEST_ENABLE_SUBMDSPAN
        OR FORGE_TEST_ENABLE_LINALG
        OR FORGE_TEST_ENABLE_ANY_FORGE
        OR FORGE_TEST_ENABLE_NATIVE_HANDOFF)
    # GoogleTest is a build-only test dependency. Do not leak its install rules
    # into a CC Forge prefix when tests and installation are enabled together.
    set(INSTALL_GTEST OFF CACHE BOOL
        "Do not install CC Forge's GoogleTest dependency" FORCE)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/googletest ${CMAKE_CURRENT_BINARY_DIR}/googletest)
endif()

set(FORGE_TEST_TIMEOUT_SECONDS 300 CACHE STRING
    "Timeout in seconds for individual CC Forge tests")

set(FORGE_TEST_ENABLE_STD_TARGET OFF)
if(FORGE_TEST_ENABLE_EXECUTION
        OR FORGE_TEST_ENABLE_SIMD
        OR FORGE_TEST_ENABLE_UNIQUE_RESOURCE
        OR FORGE_TEST_ENABLE_SUBMDSPAN
        OR FORGE_TEST_ENABLE_LINALG
        OR FORGE_TEST_ENABLE_NATIVE_HANDOFF)
    set(FORGE_TEST_ENABLE_STD_TARGET ON)
endif()

set(FORGE_TEST_BUILD_CONFIG_ARGS)
if(CMAKE_CONFIGURATION_TYPES)
    list(APPEND FORGE_TEST_BUILD_CONFIG_ARGS --config $<CONFIG>)
endif()

include(CheckCXXSourceCompiles)

function(forge_detect_cxx_source_compiles result_var source_file)
    unset(${result_var} CACHE)
    unset(${result_var})
    file(READ "${source_file}" _forge_probe_source)
    check_cxx_source_compiles("${_forge_probe_source}" ${result_var})
    set(${result_var} "${${result_var}}" PARENT_SCOPE)
endfunction()

function(forge_expect_cxx_source_compiles result_var source_file should_compile failure_message)
    set(_forge_saved_required_flags "${CMAKE_REQUIRED_FLAGS}")
    if(MSVC AND NOT CMAKE_REQUIRED_FLAGS MATCHES "(^| )/Zc:__cplusplus($| )")
        set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} /Zc:__cplusplus")
    endif()
    forge_detect_cxx_source_compiles(${result_var} "${source_file}")
    set(CMAKE_REQUIRED_FLAGS "${_forge_saved_required_flags}")
    unset(_forge_saved_required_flags)

    if(${should_compile})
        if(NOT ${result_var})
            message(FATAL_ERROR "${failure_message}")
        endif()
    else()
        if(${result_var})
            message(FATAL_ERROR "${failure_message}")
        endif()
    endif()
endfunction()

function(forge_add_runtime_test target_name source_file)
    set(test_name "${target_name}")
    if(ARGC GREATER 2)
        set(test_name "${ARGV2}")
    endif()

    add_executable(${target_name} ${source_file})
    target_link_libraries(${target_name} PRIVATE gtest_main forge::forge)
    add_test(NAME ${test_name} COMMAND ${target_name})
    set_tests_properties(${test_name} PROPERTIES TIMEOUT ${FORGE_TEST_TIMEOUT_SECONDS})
endfunction()

function(forge_add_compile_probe target_name source_file)
    set(test_name "${target_name}_compile")
    if(ARGC GREATER 2)
        set(test_name "${ARGV2}")
    endif()

    add_executable(${target_name} EXCLUDE_FROM_ALL ${source_file})
    target_link_libraries(${target_name} PRIVATE forge::forge)
    add_test(
        NAME ${test_name}
        COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} ${FORGE_TEST_BUILD_CONFIG_ARGS} --target ${target_name})
    set_tests_properties(${test_name} PROPERTIES TIMEOUT ${FORGE_TEST_TIMEOUT_SECONDS})
endfunction()

find_library(FORGE_TEST_TBB_LIBRARY NAMES tbb)

if(FORGE_TEST_ENABLE_EXECUTION)
    add_subdirectory(execution)
endif()
if(FORGE_TEST_ENABLE_SIMD)
    add_subdirectory(simd)
endif()
if(FORGE_TEST_ENABLE_UNIQUE_RESOURCE)
    add_subdirectory(unique_resource)
endif()
if(FORGE_TEST_ENABLE_SUBMDSPAN)
    add_subdirectory(submdspan)
endif()
if(FORGE_TEST_ENABLE_LINALG)
    add_subdirectory(linalg)
endif()
if(FORGE_TEST_ENABLE_STD_TARGET)
    add_subdirectory(std_target)
endif()
if(FORGE_TEST_ENABLE_ANY_FORGE)
    add_subdirectory(forge)
endif()
if(FORGE_TEST_ENABLE_NATIVE_HANDOFF)
    add_subdirectory(native_handoff)
endif()
