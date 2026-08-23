# Forge Tests

option(FORGE_TEST_ENABLE_EXECUTION "Build Forge std::execution tests" ON)
option(FORGE_TEST_ENABLE_SIMD "Build Forge std::simd tests" ON)
option(FORGE_TEST_ENABLE_CONSTANT_WRAPPER "Build Forge std::constant_wrapper tests" ON)
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
            OR FORGE_TEST_ENABLE_FORGE_ERASURE))
    set(FORGE_TEST_ENABLE_ANY_FORGE ON)
endif()

if(FORGE_TEST_ENABLE_EXECUTION
        OR FORGE_TEST_ENABLE_SIMD
        OR FORGE_TEST_ENABLE_CONSTANT_WRAPPER
        OR FORGE_TEST_ENABLE_UNIQUE_RESOURCE
        OR FORGE_TEST_ENABLE_SUBMDSPAN
        OR FORGE_TEST_ENABLE_LINALG
        OR FORGE_TEST_ENABLE_ANY_FORGE
        OR FORGE_TEST_ENABLE_NATIVE_HANDOFF)
    set(_FORGE_GTEST_SOURCE_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/googletest")
    if(NOT EXISTS "${_FORGE_GTEST_SOURCE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR
            "CC Forge tests require 3rdparty/googletest. Initialize it with: "
            "git submodule update --init --depth 1 -- 3rdparty/googletest")
    endif()

    # GoogleTest is a build-only test dependency. Build only gtest, and do not
    # leak its install rules into a CC Forge prefix when tests and installation
    # are enabled together.
    set(BUILD_GMOCK OFF CACHE BOOL
        "CC Forge tests do not use GoogleMock" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL
        "Do not install CC Forge's GoogleTest dependency" FORCE)
    add_subdirectory("${_FORGE_GTEST_SOURCE_DIR}"
        "${CMAKE_CURRENT_BINARY_DIR}/googletest")
    unset(_FORGE_GTEST_SOURCE_DIR)
endif()

set(FORGE_TEST_TIMEOUT_SECONDS 300 CACHE STRING
    "Timeout in seconds for individual CC Forge tests")

add_subdirectory(probe_fingerprint)

if(UNIX)
    add_test(
        NAME verification_scratch_policy
        COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/test_verification_scratch.sh")
    set_tests_properties(
        verification_scratch_policy PROPERTIES
        TIMEOUT ${FORGE_TEST_TIMEOUT_SECONDS})

    add_test(
        NAME windows_matrix_version_parsing
        COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/test_windows_matrix_versions.sh")
    set_tests_properties(
        windows_matrix_version_parsing PROPERTIES
        TIMEOUT ${FORGE_TEST_TIMEOUT_SECONDS})

    add_test(
        NAME windows_ssh_transport
        COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/test_windows_ssh_transport.sh")
    set_tests_properties(
        windows_ssh_transport PROPERTIES
        TIMEOUT ${FORGE_TEST_TIMEOUT_SECONDS})

    add_test(
        NAME sanitizer_retry_policy
        COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/test_sanitizer_retry_policy.sh")
    set_tests_properties(
        sanitizer_retry_policy PROPERTIES
        TIMEOUT ${FORGE_TEST_TIMEOUT_SECONDS})

    add_test(
        NAME io_uring_runtime_gate_policy
        COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/test_io_uring_runtime_gate.sh")
    set_tests_properties(
        io_uring_runtime_gate_policy PROPERTIES
        TIMEOUT ${FORGE_TEST_TIMEOUT_SECONDS})
endif()

if(WIN32)
    add_test(
        NAME forge_windows_verification_helpers
        COMMAND powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass
            -File "${CMAKE_CURRENT_SOURCE_DIR}/test_windows_verification.ps1")
    set_tests_properties(
        forge_windows_verification_helpers PROPERTIES
        TIMEOUT ${FORGE_TEST_TIMEOUT_SECONDS})
endif()

set(FORGE_TEST_ENABLE_STD_TARGET OFF)
if(FORGE_TEST_ENABLE_EXECUTION
        OR FORGE_TEST_ENABLE_SIMD
        OR FORGE_TEST_ENABLE_CONSTANT_WRAPPER
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
    set_tests_properties(${test_name} PROPERTIES
        RESOURCE_LOCK forge_main_build
        TIMEOUT ${FORGE_TEST_TIMEOUT_SECONDS})
endfunction()

find_library(FORGE_TEST_TBB_LIBRARY NAMES tbb)

if(FORGE_TEST_ENABLE_EXECUTION)
    if(FORGE_FORCE_SENDERS_BACKPORT
            OR (NOT FORGE_SENDERS_FULL AND NOT FORGE_SENDERS_PARTIAL))
        add_subdirectory(execution)
    else()
        message(STATUS
            "Forge: execution backport conformance tests skipped because the native standard library stands aside")
    endif()
endif()
if(FORGE_TEST_ENABLE_SIMD)
    if(FORGE_FORCE_SIMD_BACKPORT
            OR (NOT FORGE_SIMD_FULL AND NOT FORGE_SIMD_PARTIAL))
        add_subdirectory(simd)
    else()
        message(STATUS
            "Forge: SIMD backport conformance tests skipped because the native standard library stands aside")
    endif()
endif()
if(FORGE_TEST_ENABLE_CONSTANT_WRAPPER)
    if(FORGE_FORCE_CONSTANT_WRAPPER_BACKPORT
            OR FORGE_CONSTANT_WRAPPER_FULL
            OR NOT FORGE_CONSTANT_WRAPPER_PARTIAL)
        add_subdirectory(constant_wrapper)
    else()
        message(STATUS
            "Forge: constant_wrapper conformance tests skipped for partial-native standard library")
    endif()
endif()
if(FORGE_TEST_ENABLE_UNIQUE_RESOURCE)
    add_subdirectory(unique_resource)
endif()
if(FORGE_TEST_ENABLE_SUBMDSPAN)
    add_subdirectory(submdspan)
endif()
if(FORGE_TEST_ENABLE_LINALG)
    if(FORGE_FORCE_LINALG_BACKPORT
            OR (NOT FORGE_LINALG_FULL AND NOT FORGE_LINALG_PARTIAL))
        add_subdirectory(linalg)
    else()
        message(STATUS
            "Forge: linalg backport conformance tests skipped because the native standard library stands aside")
    endif()
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
