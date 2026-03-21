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

function(forge_add_execution_compile_probe target_name source_name test_name)
    forge_add_compile_probe(
        ${target_name}
        "${FORGE_EXECUTION_TEST_ROOT}/compile/${source_name}"
        ${test_name})
    target_include_directories(${target_name} BEFORE PRIVATE "${CMAKE_SOURCE_DIR}/backport")
    if(FORGE_TEST_TBB_LIBRARY)
        target_link_libraries(${target_name} PRIVATE ${FORGE_TEST_TBB_LIBRARY})
    endif()
endfunction()

forge_add_execution_compile_probe(
    test_execution_api_core
    test_execution_api_core.cpp
    execution_api_core_compile)

forge_add_execution_runtime_test(test_execution_mvp test_execution_mvp.cpp execution_mvp)
forge_add_execution_runtime_test(
    test_execution_inline_scheduler
    test_execution_inline_scheduler.cpp
    execution_inline_scheduler)
forge_add_execution_runtime_test(test_execution_stop_token test_execution_stop_token.cpp execution_stop_token)
forge_add_execution_runtime_test(test_execution_run_loop test_execution_run_loop.cpp execution_run_loop)
forge_add_execution_runtime_test(test_execution_adaptors test_execution_adaptors.cpp execution_adaptors)
forge_add_execution_runtime_test(test_execution_wave1 test_execution_wave1.cpp execution_wave1)
forge_add_execution_runtime_test(test_execution_wave2 test_execution_wave2.cpp execution_wave2)
forge_add_execution_runtime_test(test_execution_when_all test_execution_when_all.cpp execution_when_all)
forge_add_execution_runtime_test(test_execution_wave3 test_execution_wave3.cpp execution_wave3)
