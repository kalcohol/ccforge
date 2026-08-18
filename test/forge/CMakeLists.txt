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

function(forge_expect_forge_cxx_source_compiles result_var source_name should_compile failure_message)
    set(_forge_saved_required_includes "${CMAKE_REQUIRED_INCLUDES}")
    set(_forge_saved_required_definitions "${CMAKE_REQUIRED_DEFINITIONS}")
    set(CMAKE_REQUIRED_INCLUDES
        "${CMAKE_SOURCE_DIR}/include"
        "${CMAKE_SOURCE_DIR}/backport")
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
    forge_expect_cxx_source_compiles(
        ${result_var}
        "${FORGE_COMPILE_DIR}/${source_name}"
        ${should_compile}
        "${failure_message}")
    set(CMAKE_REQUIRED_INCLUDES "${_forge_saved_required_includes}")
    set(CMAKE_REQUIRED_DEFINITIONS "${_forge_saved_required_definitions}")
    unset(_forge_saved_required_includes)
    unset(_forge_saved_required_definitions)
    unset(_forge_target_definitions)
endfunction()

if(FORGE_TEST_ENABLE_FORGE_RUNTIME
        OR (FORGE_ENABLE_FORGE_RESOURCE_POLICY
            AND FORGE_TEST_ENABLE_FORGE_RESOURCE)
        OR FORGE_TEST_ENABLE_FORGE_ERASURE)
    add_executable(
        test_forge_execution_header
        "${FORGE_COMPILE_DIR}/test_forge_execution_header.cpp")
    target_include_directories(test_forge_execution_header BEFORE PRIVATE
        "${CMAKE_SOURCE_DIR}/include"
        "${CMAKE_SOURCE_DIR}/backport")
    target_link_libraries(test_forge_execution_header PRIVATE
        forge::forge
        Threads::Threads)
    if(FORGE_TEST_TBB_LIBRARY)
        target_link_libraries(test_forge_execution_header PRIVATE ${FORGE_TEST_TBB_LIBRARY})
    endif()
    add_test(NAME forge_execution_header COMMAND test_forge_execution_header)
    set_tests_properties(forge_execution_header PROPERTIES
        TIMEOUT ${FORGE_TEST_TIMEOUT_SECONDS})
endif()

if(FORGE_TEST_ENABLE_FORGE_IO)
    forge_add_forge_compile_probe(
        test_forge_async_stream_header
        test_forge_async_stream_header.cpp
        forge_async_stream_header)
    forge_add_forge_compile_probe(
        test_forge_timer_await_header
        test_forge_timer_await_header.cpp
        forge_timer_await_header)
    forge_add_forge_test(test_forge_byte_vocabulary test_forge_byte_vocabulary.cpp forge_byte_vocabulary)
    forge_add_forge_test(test_forge_memory_streams test_forge_memory_streams.cpp forge_memory_streams)
    forge_add_forge_test(test_forge_stream_concepts test_forge_stream_concepts.cpp forge_stream_concepts)
    forge_add_forge_test(test_forge_async_stream test_forge_async_stream.cpp forge_async_stream)
    forge_add_forge_test(test_forge_coro_io test_forge_coro_io.cpp forge_coro_io)
    forge_add_forge_test(test_forge_coro_interop test_forge_coro_interop.cpp forge_coro_interop)
    forge_add_forge_test(test_forge_coro_combinators test_forge_coro_combinators.cpp forge_coro_combinators)
    forge_add_forge_test(test_forge_timer_await test_forge_timer_await.cpp forge_timer_await)
    forge_add_forge_test(test_forge_context_await test_forge_context_await.cpp forge_context_await)
endif()

if(FORGE_TEST_ENABLE_FORGE_RUNTIME)
    forge_add_forge_test(test_forge_any_stop_token test_forge_any_stop_token.cpp forge_any_stop_token)
    forge_add_forge_test(test_forge_thread_pool test_forge_thread_pool.cpp forge_thread_pool)
    target_compile_definitions(
        test_forge_thread_pool
        PRIVATE FORGE_STATIC_THREAD_POOL_ENABLE_TEST_HOOKS)
    forge_add_forge_test(test_forge_start_detached test_forge_start_detached.cpp forge_start_detached)
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
    include(CheckLinkerFlag)
    check_linker_flag(CXX "-Wl,--wrap=epoll_ctl" FORGE_TEST_HAS_EPOLL_CTL_WRAP)
    if(FORGE_TEST_HAS_EPOLL_CTL_WRAP)
        target_compile_definitions(test_forge_io_context PRIVATE FORGE_TEST_HAS_EPOLL_CTL_WRAP=1)
        target_link_options(test_forge_io_context PRIVATE "-Wl,--wrap=epoll_ctl")
    else()
        message(STATUS
            "Forge: epoll_ctl failure-injection test skipped because the linker lacks --wrap")
    endif()
endif()

if(FORGE_HAS_FORGE_IO_WINDOWS_IOCP_BACKEND AND FORGE_TEST_ENABLE_FORGE_IO)
    forge_add_forge_test(test_forge_io_iocp test_forge_io_iocp.cpp forge_io_iocp)
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
