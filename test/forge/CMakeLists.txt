set(FORGE_TEST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(FORGE_RUNTIME_DIR "${FORGE_TEST_ROOT}/runtime")

function(forge_add_forge_test target_name source_name test_name)
    forge_add_runtime_test(
        ${target_name}
        "${FORGE_RUNTIME_DIR}/${source_name}"
        ${test_name})
    target_include_directories(${target_name} BEFORE PRIVATE
        "${CMAKE_SOURCE_DIR}/include"
        "${CMAKE_SOURCE_DIR}/backport")
    find_library(FORGE_TBB_LIB NAMES tbb)
    if(FORGE_TBB_LIB)
        target_link_libraries(${target_name} PRIVATE ${FORGE_TBB_LIB})
    endif()
endfunction()

forge_add_forge_test(test_forge_thread_pool test_forge_thread_pool.cpp forge_thread_pool)
forge_add_forge_test(test_forge_task test_forge_task.cpp forge_task)
forge_add_forge_test(test_forge_any_receiver test_forge_any_receiver.cpp forge_any_receiver)
