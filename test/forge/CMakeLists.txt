set(FORGE_TEST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(FORGE_COMPILE_DIR "${FORGE_TEST_ROOT}/compile")
set(FORGE_RUNTIME_DIR "${FORGE_TEST_ROOT}/runtime")

find_package(Threads REQUIRED)

function(forge_add_forge_test target_name source_name test_name)
    forge_add_runtime_test(
        ${target_name}
        "${FORGE_RUNTIME_DIR}/${source_name}"
        ${test_name})
    target_include_directories(${target_name} BEFORE PRIVATE
        "${CMAKE_SOURCE_DIR}/include"
        "${CMAKE_SOURCE_DIR}/backport")
    target_link_libraries(${target_name} PRIVATE Threads::Threads)
    if(FORGE_TEST_TBB_LIBRARY)
        target_link_libraries(${target_name} PRIVATE ${FORGE_TEST_TBB_LIBRARY})
    endif()
endfunction()

function(forge_add_forge_compile_probe target_name source_name test_name)
    forge_add_compile_probe(
        ${target_name}
        "${FORGE_COMPILE_DIR}/${source_name}"
        ${test_name})
    target_include_directories(${target_name} BEFORE PRIVATE
        "${CMAKE_SOURCE_DIR}/include"
        "${CMAKE_SOURCE_DIR}/backport")
    target_link_libraries(${target_name} PRIVATE Threads::Threads)
    if(FORGE_TEST_TBB_LIBRARY)
        target_link_libraries(${target_name} PRIVATE ${FORGE_TEST_TBB_LIBRARY})
    endif()
endfunction()

if(FORGE_TEST_ENABLE_FORGE_RUNTIME
        OR (FORGE_ENABLE_FORGE_RESOURCE_POLICY
            AND FORGE_TEST_ENABLE_FORGE_RESOURCE)
        OR FORGE_TEST_ENABLE_FORGE_ERASURE)
    forge_add_forge_compile_probe(
        test_forge_execution_header
        test_forge_execution_header.cpp
        forge_execution_header)
endif()

if(FORGE_TEST_ENABLE_FORGE_RUNTIME)
    forge_add_forge_test(test_forge_thread_pool test_forge_thread_pool.cpp forge_thread_pool)
    forge_add_forge_test(test_forge_single_thread_context test_forge_single_thread_context.cpp forge_single_thread_context)
    forge_add_forge_test(test_forge_timer_context test_forge_timer_context.cpp forge_timer_context)
    forge_add_forge_test(test_forge_runtime_context test_forge_runtime_context.cpp forge_runtime_context)
    forge_add_forge_test(test_forge_task test_forge_task.cpp forge_task)
    forge_add_forge_test(test_forge_async_scope test_forge_async_scope.cpp forge_async_scope)
    forge_add_forge_test(test_forge_channel test_forge_channel.cpp forge_channel)
    forge_add_forge_test(test_forge_resource_context test_forge_resource_context.cpp forge_resource_context)
    forge_add_forge_test(test_forge_strand test_forge_strand.cpp forge_strand)
    forge_add_forge_test(test_forge_runtime_stress test_forge_runtime_stress.cpp forge_runtime_stress)
    forge_add_forge_test(test_forge_scheduler_roundtrip test_forge_scheduler_roundtrip.cpp forge_scheduler_roundtrip)
    forge_add_forge_test(test_forge_wait_result test_forge_wait_result.cpp forge_wait_result)
endif()

if(FORGE_HAS_FORGE_IO_LINUX_EPOLL_BACKEND AND FORGE_TEST_ENABLE_FORGE_IO)
    forge_add_forge_test(test_forge_io_context test_forge_io_context.cpp forge_io_context)
endif()

if(FORGE_HAS_FORGE_IO_WINDOWS_IOCP_BACKEND AND FORGE_TEST_ENABLE_FORGE_IO)
    forge_add_forge_test(test_forge_io_iocp test_forge_io_iocp.cpp forge_io_iocp)
endif()

if(FORGE_HAS_FORGE_ACCEL_BACKEND AND FORGE_TEST_ENABLE_FORGE_ACCEL)
    forge_add_forge_compile_probe(
        test_forge_accel_vocabulary
        test_forge_accel_vocabulary.cpp
        forge_accel_vocabulary)
    forge_add_forge_test(test_forge_accel_context test_forge_accel_context.cpp forge_accel_context)
    forge_add_forge_test(test_forge_accel_copy test_forge_accel_copy.cpp forge_accel_copy)
    forge_add_forge_test(test_forge_accel_event test_forge_accel_event.cpp forge_accel_event)
    forge_add_forge_test(test_forge_accel_submit test_forge_accel_submit.cpp forge_accel_submit)
    forge_add_forge_test(test_forge_accel_request test_forge_accel_request.cpp forge_accel_request)
    forge_add_forge_test(test_forge_accel_protocol test_forge_accel_protocol.cpp forge_accel_protocol)
    forge_add_forge_test(test_forge_accel_device test_forge_accel_device.cpp forge_accel_device)
    forge_add_forge_test(test_forge_accel_trace test_forge_accel_trace.cpp forge_accel_trace)
    forge_add_forge_test(test_forge_accel_backend_conformance test_forge_accel_backend_conformance.cpp forge_accel_backend_conformance)
    forge_add_forge_test(test_forge_accel_model test_forge_accel_model.cpp forge_accel_model)
    forge_add_forge_test(test_forge_accel_typed_error test_forge_accel_typed_error.cpp forge_accel_typed_error)
endif()

if(FORGE_ENABLE_FORGE_RESOURCE_POLICY AND FORGE_TEST_ENABLE_FORGE_RESOURCE)
    forge_add_forge_test(test_forge_resource_policy test_forge_resource_policy.cpp forge_resource_policy)
endif()

if(FORGE_TEST_ENABLE_FORGE_ERASURE)
    forge_add_forge_test(test_forge_any_scheduler test_forge_any_scheduler.cpp forge_any_scheduler)
    forge_add_forge_test(test_forge_erased_sender test_forge_erased_sender.cpp forge_erased_sender)
    forge_add_forge_test(test_forge_any_sender test_forge_any_sender.cpp forge_any_sender)
    forge_add_forge_test(test_forge_any_receiver test_forge_any_receiver.cpp forge_any_receiver)
endif()
