# Forge Tests

option(FORGE_TEST_ONLY_EXECUTION "Build only Forge execution tests" OFF)
option(FORGE_TEST_ENABLE_EXECUTION "Build Forge std::execution tests" ON)
option(FORGE_TEST_ENABLE_SIMD "Build Forge std::simd tests" ON)
option(FORGE_TEST_ENABLE_UNIQUE_RESOURCE "Build Forge std::unique_resource tests" ON)
option(FORGE_TEST_ENABLE_SUBMDSPAN "Build Forge std::submdspan tests" ON)
option(FORGE_TEST_ENABLE_LINALG "Build Forge std::linalg tests" ON)
option(FORGE_TEST_ENABLE_FORGE "Build Forge extension tests" ON)
option(FORGE_TEST_ENABLE_NATIVE_HANDOFF "Build Forge native handoff tests" ON)

set(_FORGE_TEST_BUILD_EXECUTION ${FORGE_TEST_ENABLE_EXECUTION})
set(_FORGE_TEST_BUILD_SIMD ${FORGE_TEST_ENABLE_SIMD})
set(_FORGE_TEST_BUILD_UNIQUE_RESOURCE ${FORGE_TEST_ENABLE_UNIQUE_RESOURCE})
set(_FORGE_TEST_BUILD_SUBMDSPAN ${FORGE_TEST_ENABLE_SUBMDSPAN})
set(_FORGE_TEST_BUILD_LINALG ${FORGE_TEST_ENABLE_LINALG})
set(_FORGE_TEST_BUILD_FORGE ${FORGE_TEST_ENABLE_FORGE})
set(_FORGE_TEST_BUILD_NATIVE_HANDOFF ${FORGE_TEST_ENABLE_NATIVE_HANDOFF})

if(FORGE_TEST_ONLY_EXECUTION)
    set(_FORGE_TEST_BUILD_EXECUTION ON)
    set(_FORGE_TEST_BUILD_SIMD OFF)
    set(_FORGE_TEST_BUILD_UNIQUE_RESOURCE OFF)
    set(_FORGE_TEST_BUILD_SUBMDSPAN OFF)
    set(_FORGE_TEST_BUILD_LINALG OFF)
    set(_FORGE_TEST_BUILD_FORGE OFF)
    set(_FORGE_TEST_BUILD_NATIVE_HANDOFF OFF)
endif()

if(_FORGE_TEST_BUILD_EXECUTION
        OR _FORGE_TEST_BUILD_SIMD
        OR _FORGE_TEST_BUILD_UNIQUE_RESOURCE
        OR _FORGE_TEST_BUILD_SUBMDSPAN
        OR _FORGE_TEST_BUILD_LINALG
        OR _FORGE_TEST_BUILD_FORGE
        OR _FORGE_TEST_BUILD_NATIVE_HANDOFF)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/googletest ${CMAKE_CURRENT_BINARY_DIR}/googletest)
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
    forge_detect_cxx_source_compiles(${result_var} "${source_file}")

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
endfunction()

find_library(FORGE_TEST_TBB_LIBRARY NAMES tbb)

if(_FORGE_TEST_BUILD_EXECUTION)
    add_subdirectory(execution)
endif()
if(_FORGE_TEST_BUILD_SIMD)
    add_subdirectory(simd)
endif()
if(_FORGE_TEST_BUILD_UNIQUE_RESOURCE)
    add_subdirectory(unique_resource)
endif()
if(_FORGE_TEST_BUILD_SUBMDSPAN)
    add_subdirectory(submdspan)
endif()
if(_FORGE_TEST_BUILD_LINALG)
    add_subdirectory(linalg)
endif()
if(_FORGE_TEST_BUILD_FORGE)
    add_subdirectory(forge)
endif()
if(_FORGE_TEST_BUILD_NATIVE_HANDOFF)
    add_subdirectory(native_handoff)
endif()

unset(_FORGE_TEST_BUILD_EXECUTION)
unset(_FORGE_TEST_BUILD_SIMD)
unset(_FORGE_TEST_BUILD_UNIQUE_RESOURCE)
unset(_FORGE_TEST_BUILD_SUBMDSPAN)
unset(_FORGE_TEST_BUILD_LINALG)
unset(_FORGE_TEST_BUILD_FORGE)
unset(_FORGE_TEST_BUILD_NATIVE_HANDOFF)
