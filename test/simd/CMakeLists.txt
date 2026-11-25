set(FORGE_SIMD_TEST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(FORGE_SIMD_SUPPORT_DIR "${FORGE_SIMD_TEST_ROOT}/support")
set(FORGE_SIMD_RUNTIME_DIR "${FORGE_SIMD_TEST_ROOT}/runtime")
set(FORGE_SIMD_COMPILE_DIR "${FORGE_SIMD_TEST_ROOT}/compile")
set(FORGE_SIMD_CONFIGURE_PROBE_DIR "${FORGE_SIMD_TEST_ROOT}/configure_probes")
set(FORGE_SIMD_NATIVE_HANDOFF_INCLUDE_DIR "${FORGE_SIMD_COMPILE_DIR}/native_handoff_include")

function(forge_add_simd_runtime_test target_name source_name)
    forge_add_runtime_test(${target_name} "${FORGE_SIMD_RUNTIME_DIR}/${source_name}" ${ARGN})
    target_include_directories(${target_name} PRIVATE "${FORGE_SIMD_SUPPORT_DIR}")
endfunction()

function(forge_add_simd_compile_probe target_name source_name)
    forge_add_compile_probe(${target_name} "${FORGE_SIMD_COMPILE_DIR}/${source_name}" ${ARGN})
    target_include_directories(${target_name} PRIVATE "${FORGE_SIMD_SUPPORT_DIR}")
endfunction()

function(forge_add_simd_configure_probe result_var source_name should_compile failure_message)
    set(CMAKE_REQUIRED_INCLUDES "")
    forge_expect_cxx_source_compiles(
        ${result_var}
        "${FORGE_SIMD_CONFIGURE_PROBE_DIR}/${source_name}"
        ${should_compile}
        "${failure_message}"
        ${ARGN})
endfunction()

add_subdirectory(configure_probes)

add_subdirectory(runtime)
add_subdirectory(compile)

if(NOT MSVC AND CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$"
        AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$"
        AND NOT FORGE_SIMD_FULL AND NOT FORGE_SIMD_PARTIAL)
    add_subdirectory(native_abi_mixed_tu)
endif()
