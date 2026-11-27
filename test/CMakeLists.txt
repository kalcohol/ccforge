# Forge Tests

option(FORGE_TEST_ENABLE_EXECUTION "Build Forge std::execution tests" ON)
option(FORGE_TEST_ENABLE_SIMD "Build Forge std::simd tests" ON)
option(FORGE_TEST_ENABLE_CONSTANT_WRAPPER "Build Forge std::constant_wrapper tests" ON)
option(FORGE_TEST_ENABLE_UNIQUE_RESOURCE "Build forge::unique_resource tests" ON)
option(FORGE_TEST_ENABLE_SUBMDSPAN "Build Forge std::submdspan tests" ON)
option(FORGE_TEST_ENABLE_LINALG "Build Forge std::linalg tests" ON)
option(FORGE_TEST_ENABLE_HIVE "Build Forge std::hive tests" ON)
option(FORGE_TEST_ENABLE_FORGE "Build Forge extension tests" ON)
option(FORGE_TEST_ENABLE_FORGE_RUNTIME "Build Forge runtime extension tests" ON)
option(FORGE_TEST_ENABLE_FORGE_RESOURCE "Build Forge resource policy extension tests" ON)
option(FORGE_TEST_ENABLE_FORGE_IO "Build Forge IO extension tests when available" ON)
option(FORGE_TEST_ENABLE_FORGE_ERASURE "Build Forge erasure extension tests" ON)
option(FORGE_TEST_ENABLE_NATIVE_HANDOFF "Build Forge native handoff tests" ON)
# Lane assertions for the native execution policy modes (NativePolicyTests.cmake).
option(FORGE_TEST_REQUIRE_NATIVE_POLICIES
    "Require all four native execution policy objects in the default mode" OFF)
option(FORGE_TEST_REQUIRE_LIBCPP_EXPERIMENTAL_POLICIES
    "Require the libc++ -fexperimental-library execution policy test variants" OFF)

set(FORGE_TEST_ENABLE_ANY_FORGE OFF)
if(FORGE_ENABLE_EXTENSIONS AND FORGE_TEST_ENABLE_FORGE
        AND (FORGE_TEST_ENABLE_FORGE_RUNTIME
            OR (FORGE_ENABLE_FORGE_RESOURCE_POLICY
                AND FORGE_TEST_ENABLE_FORGE_RESOURCE)
            OR FORGE_TEST_ENABLE_FORGE_IO
            OR FORGE_TEST_ENABLE_FORGE_ERASURE))
    set(FORGE_TEST_ENABLE_ANY_FORGE ON)
endif()

# The native-handoff group still includes full-product integration fixtures.
if(FORGE_TEST_ENABLE_EXECUTION
        OR FORGE_TEST_ENABLE_SIMD
        OR FORGE_TEST_ENABLE_CONSTANT_WRAPPER
        OR FORGE_TEST_ENABLE_SUBMDSPAN
        OR FORGE_TEST_ENABLE_LINALG
        OR FORGE_TEST_ENABLE_HIVE
        OR FORGE_TEST_ENABLE_ANY_FORGE
        OR (FORGE_ENABLE_EXTENSIONS AND (FORGE_TEST_ENABLE_UNIQUE_RESOURCE
            OR FORGE_TEST_ENABLE_NATIVE_HANDOFF)))
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
        SKIP_RETURN_CODE 77
        TIMEOUT ${FORGE_TEST_TIMEOUT_SECONDS})

    add_test(
        NAME macos_ssh_transport
        COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/test_macos_ssh_transport.sh")
    set_tests_properties(
        macos_ssh_transport PROPERTIES
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
        OR FORGE_TEST_ENABLE_SUBMDSPAN
        OR FORGE_TEST_ENABLE_LINALG
        OR FORGE_TEST_ENABLE_HIVE
        OR FORGE_TEST_ENABLE_NATIVE_HANDOFF)
    set(FORGE_TEST_ENABLE_STD_TARGET ON)
endif()

set(FORGE_TEST_BUILD_CONFIG_ARGS)
if(CMAKE_CONFIGURATION_TYPES)
    list(APPEND FORGE_TEST_BUILD_CONFIG_ARGS --config $<CONFIG>)
endif()

include(CheckCXXSourceCompiles)

# Rejection probes require an error independent of the caller's warning flags.
set(_forge_rejection_compile_options)
if(MSVC)
    set(_forge_rejection_compile_options /WX- /w)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang|Zig)$")
    set(_forge_rejection_compile_options -w)
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/TestOwner.cmake")

# Rule 5 limits this helper to forge_detect_cxx_source_compiles and
# forge_expect_cxx_source_compiles, which prepare the probe inputs first.
function(_forge_detect_cxx_source_compiles result_var source_file)
    unset(${result_var} CACHE)
    unset(${result_var})
    file(READ "${source_file}" _forge_probe_source)
    _forge_check_prepared_cxx_source_compiles("${_forge_probe_source}" ${result_var})
    set(${result_var} "${${result_var}}" PARENT_SCOPE)
endfunction()

function(forge_detect_cxx_source_compiles result_var source_file)
    _forge_prepare_test_probe(${ARGN})
    _forge_detect_cxx_source_compiles(${result_var} "${source_file}")
    set(${result_var} "${${result_var}}" PARENT_SCOPE)
endfunction()

unset(FORGE_REJECTION_WARNING_CONTROL CACHE)
function(forge_check_rejection_warning_control)
    if(DEFINED FORGE_REJECTION_WARNING_CONTROL)
        return()
    endif()

    # Exercise the rejection flags against configuration-specific warning flags.
    if(NOT CMAKE_TRY_COMPILE_CONFIGURATION)
        set(CMAKE_TRY_COMPILE_CONFIGURATION Debug)
    endif()
    string(TOUPPER "${CMAKE_TRY_COMPILE_CONFIGURATION}" _forge_probe_config)
    if(MSVC)
        string(APPEND CMAKE_CXX_FLAGS_${_forge_probe_config} " /W4 /WX")
    else()
        string(APPEND CMAKE_CXX_FLAGS_${_forge_probe_config} " -Wunused-variable -Werror")
    endif()
    _forge_check_prepared_cxx_source_compiles([=[
        int main() {
            int unused = 0;
            return 0;
        }
    ]=] FORGE_REJECTION_WARNING_CONTROL)
    if(NOT FORGE_REJECTION_WARNING_CONTROL)
        message(FATAL_ERROR "A warning-only C++ source must compile with the rejection probe flags")
    endif()
endfunction()

function(forge_expect_cxx_source_compiles result_var source_file should_compile failure_message)
    _forge_prepare_test_probe(${ARGN})
    if(should_compile)
        set(CMAKE_TRY_COMPILE_TARGET_TYPE EXECUTABLE)
    else()
        set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
        set(CMAKE_REQUIRED_LINK_OPTIONS "")
        if(_forge_rejection_compile_options)
            # try_compile passes these to add_definitions, after the global
            # and configuration-specific flags in the generated project.
            list(APPEND CMAKE_REQUIRED_DEFINITIONS ${_forge_rejection_compile_options})
            forge_check_rejection_warning_control()
        endif()
    endif()
    _forge_detect_cxx_source_compiles(${result_var} "${source_file}")

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

set(FORGE_TEST_GTEST_CASES_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/RunGoogleTestCases.cmake")

function(forge_add_runtime_test target_name source_file)
    _forge_test_registration(test_name owner_target "${target_name}" ${ARGN})

    add_executable(${target_name} ${source_file})
    target_link_libraries(${target_name} PRIVATE gtest_main ${owner_target})
    if(FORGE_TEST_REQUIRE_GTEST_CASES)
        # Set by the caller for a CTest that qualifies a configuration.
        add_test(NAME ${test_name} COMMAND ${CMAKE_COMMAND}
            -DTEST_BINARY=$<TARGET_FILE:${target_name}>
            -P "${FORGE_TEST_GTEST_CASES_SCRIPT}")
    else()
        add_test(NAME ${test_name} COMMAND ${target_name})
    endif()
    set_tests_properties(${test_name} PROPERTIES TIMEOUT ${FORGE_TEST_TIMEOUT_SECONDS})
endfunction()

include("${CMAKE_CURRENT_SOURCE_DIR}/CompileProbe.cmake")

find_library(FORGE_TEST_TBB_LIBRARY NAMES tbb)

if(FORGE_TEST_ENABLE_EXECUTION OR FORGE_TEST_ENABLE_LINALG OR FORGE_TEST_ENABLE_ANY_FORGE
        OR FORGE_TEST_REQUIRE_NATIVE_POLICIES OR FORGE_TEST_REQUIRE_LIBCPP_EXPERIMENTAL_POLICIES)
    include("${CMAKE_CURRENT_SOURCE_DIR}/NativePolicyTests.cmake")
endif()

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
if(FORGE_ENABLE_EXTENSIONS AND FORGE_TEST_ENABLE_UNIQUE_RESOURCE)
    add_subdirectory(unique_resource)
endif()
if(FORGE_TEST_ENABLE_SUBMDSPAN)
    # Keep portable native tests enabled; gate implementation probes separately.
    add_test(
        NAME submdspan_test_gate_matrix
        COMMAND ${CMAKE_COMMAND}
            -DFORGE_ROOT_DIR=${CMAKE_SOURCE_DIR}
            -DBINARY_ROOT=${CMAKE_CURRENT_BINARY_DIR}/submdspan-gates
            "-DGENERATOR=${CMAKE_GENERATOR}"
            "-DGENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
            "-DGENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/submdspan/verify_test_gates.cmake)
    set_tests_properties(submdspan_test_gate_matrix PROPERTIES
        TIMEOUT ${FORGE_TEST_TIMEOUT_SECONDS})
    add_subdirectory(submdspan)
endif()
if(FORGE_TEST_ENABLE_LINALG)
    add_subdirectory(linalg)
endif()
if(FORGE_TEST_ENABLE_HIVE)
    include("${CMAKE_CURRENT_SOURCE_DIR}/hive/RegisterTests.cmake")
endif()
if(FORGE_TEST_ENABLE_STD_TARGET)
    add_subdirectory(std_target)
endif()
if(FORGE_TEST_ENABLE_ANY_FORGE OR (FORGE_ENABLE_EXTENSIONS AND FORGE_TEST_ENABLE_SIMD))
    add_subdirectory(forge)
endif()
if(FORGE_ENABLE_EXTENSIONS AND FORGE_TEST_ENABLE_NATIVE_HANDOFF)
    add_subdirectory(native_handoff)
endif()

if(FORGE_TEST_EXPERIMENTAL_POLICIES)
    # Wherever the flagged mode is enabled, each group that applies must have
    # registered its variants with the flag (NativePolicyTests.cmake).
    set(_execution_injected OFF)
    if(FORGE_FORCE_SENDERS_BACKPORT OR (NOT FORGE_SENDERS_FULL AND NOT FORGE_SENDERS_PARTIAL))
        set(_execution_injected ON)
    endif()
    if(FORGE_TEST_ENABLE_EXECUTION AND _execution_injected)
        forge_require_experimental_policy_tests("${CMAKE_CURRENT_SOURCE_DIR}/execution"
            execution_bulk_native_policy_experimental
            execution_bulk_policy_constraints_experimental_compile)
    endif()
    if(FORGE_TEST_ENABLE_ANY_FORGE AND FORGE_TEST_ENABLE_FORGE_RUNTIME AND _execution_injected)
        forge_require_experimental_policy_tests("${CMAKE_CURRENT_SOURCE_DIR}/forge"
            forge_serial_bulk_experimental)
    endif()
    if(FORGE_TEST_ENABLE_LINALG AND FORGE_BASE_MDSPAN_AVAILABLE AND FORGE_LINALG_HAS_MDSPAN_FEATURE
            AND (FORGE_FORCE_LINALG_BACKPORT OR (NOT FORGE_LINALG_FULL AND NOT FORGE_LINALG_PARTIAL)))
        forge_require_experimental_policy_tests("${CMAKE_CURRENT_SOURCE_DIR}/linalg"
            linalg_execution_policy_experimental
            linalg_policy_selection_experimental_compile
            linalg_policy_include_order_experimental_compile
            linalg_policy_include_order_experimental)
        if(NOT FORGE_TEST_DEFAULT_POLICY_OBJECTS)
            # The existing draft-surface target moves to the flagged mode.
            forge_require_experimental_policy_tests("${CMAKE_CURRENT_SOURCE_DIR}/linalg"
                linalg_draft_surface_compile)
        endif()
    endif()
    unset(_execution_injected)
endif()
