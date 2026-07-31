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
    set_tests_properties(${target_name} PROPERTIES TIMEOUT ${FORGE_TEST_TIMEOUT_SECONDS})
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
    set_tests_properties(${test_name} PROPERTIES
        RESOURCE_LOCK forge_main_build
        TIMEOUT ${FORGE_TEST_TIMEOUT_SECONDS})
endfunction()

function(forge_add_simd_configure_probe result_var source_name should_compile failure_message)
    forge_expect_cxx_source_compiles(
        ${result_var}
        "${FORGE_SIMD_CONFIGURE_PROBE_DIR}/${source_name}"
        ${should_compile}
        "${failure_message}")
endfunction()

set(_FORGE_SIMD_SAVED_CMAKE_REQUIRED_INCLUDES "${CMAKE_REQUIRED_INCLUDES}")
set(_FORGE_SIMD_SAVED_CMAKE_REQUIRED_DEFINITIONS "${CMAKE_REQUIRED_DEFINITIONS}")
set(CMAKE_REQUIRED_INCLUDES
    "${CMAKE_SOURCE_DIR}/backport"
    "${CMAKE_SOURCE_DIR}/include")
foreach(_forge_required_target IN ITEMS forge_std forge)
    if(TARGET ${_forge_required_target})
        get_target_property(
            _forge_target_definitions
            ${_forge_required_target}
            INTERFACE_COMPILE_DEFINITIONS)
        if(_forge_target_definitions)
            foreach(_forge_definition IN LISTS _forge_target_definitions)
                list(APPEND CMAKE_REQUIRED_DEFINITIONS "-D${_forge_definition}")
            endforeach()
        endif()
    endif()
endforeach()
add_subdirectory(configure_probes)
set(CMAKE_REQUIRED_INCLUDES "${_FORGE_SIMD_SAVED_CMAKE_REQUIRED_INCLUDES}")
set(CMAKE_REQUIRED_DEFINITIONS "${_FORGE_SIMD_SAVED_CMAKE_REQUIRED_DEFINITIONS}")
unset(_FORGE_SIMD_SAVED_CMAKE_REQUIRED_INCLUDES)
unset(_FORGE_SIMD_SAVED_CMAKE_REQUIRED_DEFINITIONS)
unset(_forge_target_definitions)

add_subdirectory(runtime)
add_subdirectory(compile)
