# Forge Tests

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty/googletest ${CMAKE_CURRENT_BINARY_DIR}/googletest)

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

add_subdirectory(simd)
add_subdirectory(unique_resource)
add_subdirectory(execution)
add_subdirectory(submdspan)
