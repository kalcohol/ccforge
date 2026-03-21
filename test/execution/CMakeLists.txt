set(FORGE_EXECUTION_TEST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(FORGE_EXECUTION_RUNTIME_DIR "${FORGE_EXECUTION_TEST_ROOT}/runtime")

function(forge_add_execution_runtime_test target_name source_name test_name)
    forge_add_runtime_test(
        ${target_name}
        "${FORGE_EXECUTION_RUNTIME_DIR}/${source_name}"
        ${test_name})
    if(FORGE_TEST_TBB_LIBRARY)
        target_link_libraries(${target_name} PRIVATE ${FORGE_TEST_TBB_LIBRARY})
    endif()
    target_include_directories(${target_name} BEFORE PRIVATE "${CMAKE_SOURCE_DIR}/backport")
endfunction()

forge_add_execution_runtime_test(test_execution_mvp test_execution_mvp.cpp execution_mvp)
forge_add_execution_runtime_test(
    test_execution_inline_scheduler
    test_execution_inline_scheduler.cpp
    execution_inline_scheduler)
forge_add_execution_runtime_test(test_execution_stop_token test_execution_stop_token.cpp execution_stop_token)
