set(FORGE_SIMD_TEST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(FORGE_SIMD_SUPPORT_DIR "${FORGE_SIMD_TEST_ROOT}/support")
set(FORGE_SIMD_RUNTIME_DIR "${FORGE_SIMD_TEST_ROOT}/runtime")
set(FORGE_SIMD_COMPILE_DIR "${FORGE_SIMD_TEST_ROOT}/compile")
set(FORGE_SIMD_CONFIGURE_PROBE_DIR "${FORGE_SIMD_TEST_ROOT}/configure_probes")
set(FORGE_SIMD_NATIVE_HANDOFF_INCLUDE_DIR "${FORGE_SIMD_COMPILE_DIR}/native_handoff_include")

function(forge_add_simd_runtime_test target_name source_name)
    add_executable(${target_name} "${FORGE_SIMD_RUNTIME_DIR}/${source_name}")
    target_link_libraries(${target_name} PRIVATE gtest_main forge::forge)
    target_include_directories(${target_name} PRIVATE "${FORGE_SIMD_SUPPORT_DIR}")
    add_test(NAME ${target_name} COMMAND ${target_name})
endfunction()

function(forge_add_simd_compile_probe target_name source_name)
    set(test_name "${target_name}_compile")
    if(ARGC GREATER 2)
        set(test_name "${ARGV2}")
    endif()

    add_executable(${target_name} EXCLUDE_FROM_ALL "${FORGE_SIMD_COMPILE_DIR}/${source_name}")
    target_link_libraries(${target_name} PRIVATE forge::forge)
    target_include_directories(${target_name} PRIVATE "${FORGE_SIMD_SUPPORT_DIR}")
    add_test(
        NAME ${test_name}
        COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} ${FORGE_TEST_BUILD_CONFIG_ARGS} --target ${target_name})
endfunction()

function(forge_add_simd_configure_probe result_var source_name should_compile failure_message)
    forge_expect_cxx_source_compiles(
        ${result_var}
        "${FORGE_SIMD_CONFIGURE_PROBE_DIR}/${source_name}"
        ${should_compile}
        "${failure_message}")
endfunction()

set(_FORGE_SIMD_SAVED_CMAKE_REQUIRED_INCLUDES "${CMAKE_REQUIRED_INCLUDES}")
set(CMAKE_REQUIRED_INCLUDES
    "${CMAKE_SOURCE_DIR}/backport"
    "${CMAKE_SOURCE_DIR}/include")
add_subdirectory(configure_probes)
set(CMAKE_REQUIRED_INCLUDES "${_FORGE_SIMD_SAVED_CMAKE_REQUIRED_INCLUDES}")
unset(_FORGE_SIMD_SAVED_CMAKE_REQUIRED_INCLUDES)

add_subdirectory(runtime)
add_subdirectory(compile)

if(TARGET simd_example)
    add_test(NAME simd_example_smoke COMMAND simd_example)
endif()

if(TARGET simd_complete_example)
    add_test(NAME simd_complete_example_smoke COMMAND simd_complete_example)
endif()
