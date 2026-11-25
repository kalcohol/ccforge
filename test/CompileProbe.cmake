include("${CMAKE_CURRENT_LIST_DIR}/TestOwner.cmake")

function(forge_add_compile_probe target_name source_file)
    _forge_test_registration(test_name owner_target "${target_name}_compile" ${ARGN})

    add_executable(${target_name} EXCLUDE_FROM_ALL ${source_file})
    target_link_libraries(${target_name} PRIVATE ${owner_target})
    add_test(
        NAME ${test_name}
        COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} ${FORGE_TEST_BUILD_CONFIG_ARGS} --target ${target_name})
    set_tests_properties(${test_name} PROPERTIES
        RESOURCE_LOCK forge_main_build
        TIMEOUT ${FORGE_TEST_TIMEOUT_SECONDS})
endfunction()
