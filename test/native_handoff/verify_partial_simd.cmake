if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()
if(NOT DEFINED BINARY_DIR)
    message(FATAL_ERROR "BINARY_DIR is required")
endif()
if(NOT DEFINED FORGE_ROOT_DIR)
    message(FATAL_ERROR "FORGE_ROOT_DIR is required")
endif()
if(NOT DEFINED CXX_STANDARD OR CXX_STANDARD STREQUAL "")
    set(CXX_STANDARD 23)
endif()

file(REMOVE_RECURSE "${BINARY_DIR}")

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        -S "${SOURCE_DIR}"
        -B "${BINARY_DIR}"
        -DCMAKE_CXX_STANDARD=${CXX_STANDARD}
        -DFORGE_ROOT_DIR=${FORGE_ROOT_DIR}
        -DFORGE_BUILD_TESTS=OFF
        -DFORGE_BUILD_EXAMPLES=OFF
        -DFORGE_ENABLE_FORGE_IO=OFF
        -DFORGE_ENABLE_FORGE_ACCEL=OFF
    RESULT_VARIABLE configure_result
    OUTPUT_VARIABLE configure_output
    ERROR_VARIABLE configure_error)

set(configure_log "${configure_output}\n${configure_error}")
if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR "partial std::simd handoff configure failed:\n${configure_log}")
endif()

if(NOT configure_log MATCHES "std::simd native support is present but INCOMPLETE")
    message(FATAL_ERROR "partial std::simd configure did not exercise the partial-native branch:\n${configure_log}")
endif()

if(configure_log MATCHES "std::simd backport enabled")
    message(FATAL_ERROR "partial std::simd configure injected the backport instead of standing aside:\n${configure_log}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${BINARY_DIR}" --target partial_simd_consumer
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_output
    ERROR_VARIABLE build_error)

if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "partial std::simd consumer build failed:\n${build_output}\n${build_error}")
endif()
