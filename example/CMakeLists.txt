# Forge Examples

function(forge_add_example_smoke target_name)
    if(FORGE_BUILD_TESTS)
        add_test(NAME "example_${target_name}_smoke" COMMAND $<TARGET_FILE:${target_name}>)
    endif()
endfunction()

add_executable(unique_resource_example unique_resource_example.cpp)
target_link_libraries(unique_resource_example PRIVATE forge::forge)

add_executable(simd_example simd_example.cpp)
target_link_libraries(simd_example PRIVATE forge::forge)

add_executable(simd_complete_example simd_complete_example.cpp)
target_link_libraries(simd_complete_example PRIVATE forge::forge)

# std::execution (P2300 MVP) example
#
# This is built unconditionally as part of the repo's examples to ensure the
# wrapper injection shape stays functional.
find_package(Threads REQUIRED)
find_library(FORGE_EXAMPLE_TBB_LIBRARY NAMES tbb)

add_executable(execution_example execution_example.cpp)
target_link_libraries(execution_example PRIVATE forge::forge)
if(FORGE_EXAMPLE_TBB_LIBRARY)
    target_link_libraries(execution_example PRIVATE ${FORGE_EXAMPLE_TBB_LIBRARY})
endif()
target_include_directories(execution_example BEFORE PRIVATE "${CMAKE_SOURCE_DIR}/backport")

# std::submdspan (P2630) example
# Only built when the toolchain provides <mdspan> (e.g. zig/clang libc++ 18+).
# GCC 13 libstdc++ does not ship <mdspan>, so this is guarded.
include(CheckCXXSourceCompiles)
check_cxx_source_compiles("
    #include <mdspan>
    int main() { return 0; }
" FORGE_EXAMPLE_HAS_MDSPAN)

if(FORGE_EXAMPLE_HAS_MDSPAN)
    add_executable(submdspan_example submdspan_example.cpp)
    target_link_libraries(submdspan_example PRIVATE forge::forge)
    target_include_directories(submdspan_example BEFORE PRIVATE "${CMAKE_SOURCE_DIR}/backport")
endif()

# Phase 3 execution example (built unconditionally)
add_executable(execution_phase3_example execution_phase3_example.cpp)
target_link_libraries(execution_phase3_example PRIVATE forge::forge)
if(FORGE_EXAMPLE_TBB_LIBRARY)
    target_link_libraries(execution_phase3_example PRIVATE ${FORGE_EXAMPLE_TBB_LIBRARY})
endif()
target_include_directories(execution_phase3_example BEFORE PRIVATE "${CMAKE_SOURCE_DIR}/backport")

# Execution examples (unconditional)
add_executable(execution_schedulers execution_schedulers_example.cpp)
target_link_libraries(execution_schedulers PRIVATE forge::forge)
if(FORGE_EXAMPLE_TBB_LIBRARY)
    target_link_libraries(execution_schedulers PRIVATE ${FORGE_EXAMPLE_TBB_LIBRARY})
endif()
target_include_directories(execution_schedulers BEFORE PRIVATE "${CMAKE_SOURCE_DIR}/backport")

add_executable(execution_let execution_let_example.cpp)
target_link_libraries(execution_let PRIVATE forge::forge)
if(FORGE_EXAMPLE_TBB_LIBRARY)
    target_link_libraries(execution_let PRIVATE ${FORGE_EXAMPLE_TBB_LIBRARY})
endif()
target_include_directories(execution_let BEFORE PRIVATE "${CMAKE_SOURCE_DIR}/backport")

add_executable(execution_stop_token execution_stop_token_example.cpp)
target_link_libraries(execution_stop_token PRIVATE forge::forge)
if(FORGE_EXAMPLE_TBB_LIBRARY)
    target_link_libraries(execution_stop_token PRIVATE ${FORGE_EXAMPLE_TBB_LIBRARY})
endif()
target_include_directories(execution_stop_token BEFORE PRIVATE "${CMAKE_SOURCE_DIR}/backport")

add_executable(execution_phase4 execution_phase4_example.cpp)
target_link_libraries(execution_phase4 PRIVATE forge::forge)
if(FORGE_EXAMPLE_TBB_LIBRARY)
    target_link_libraries(execution_phase4 PRIVATE ${FORGE_EXAMPLE_TBB_LIBRARY})
endif()
target_include_directories(execution_phase4 BEFORE PRIVATE "${CMAKE_SOURCE_DIR}/backport")

# Forge extension examples
if(FORGE_ENABLE_FORGE_RUNTIME)
add_executable(forge_any_sender forge_any_sender_example.cpp)
target_link_libraries(forge_any_sender PRIVATE forge::forge)
if(FORGE_EXAMPLE_TBB_LIBRARY)
    target_link_libraries(forge_any_sender PRIVATE ${FORGE_EXAMPLE_TBB_LIBRARY})
endif()

add_executable(forge_any_receiver forge_any_receiver_example.cpp)
target_link_libraries(forge_any_receiver PRIVATE forge::forge)
if(FORGE_EXAMPLE_TBB_LIBRARY)
    target_link_libraries(forge_any_receiver PRIVATE ${FORGE_EXAMPLE_TBB_LIBRARY})
endif()

add_executable(forge_any_scheduler forge_any_scheduler_example.cpp)
target_link_libraries(forge_any_scheduler PRIVATE forge::forge Threads::Threads)
if(FORGE_EXAMPLE_TBB_LIBRARY)
    target_link_libraries(forge_any_scheduler PRIVATE ${FORGE_EXAMPLE_TBB_LIBRARY})
endif()

add_executable(forge_thread_pool forge_thread_pool_example.cpp)
target_link_libraries(forge_thread_pool PRIVATE forge::forge Threads::Threads)
if(FORGE_EXAMPLE_TBB_LIBRARY)
    target_link_libraries(forge_thread_pool PRIVATE ${FORGE_EXAMPLE_TBB_LIBRARY})
endif()

add_executable(forge_system_context forge_system_context_example.cpp)
target_link_libraries(forge_system_context PRIVATE forge::forge Threads::Threads)
if(FORGE_EXAMPLE_TBB_LIBRARY)
    target_link_libraries(forge_system_context PRIVATE ${FORGE_EXAMPLE_TBB_LIBRARY})
endif()

add_executable(forge_single_thread_context forge_single_thread_context_example.cpp)
target_link_libraries(forge_single_thread_context PRIVATE forge::forge Threads::Threads)
if(FORGE_EXAMPLE_TBB_LIBRARY)
    target_link_libraries(forge_single_thread_context PRIVATE ${FORGE_EXAMPLE_TBB_LIBRARY})
endif()

add_executable(forge_timer_context forge_timer_context_example.cpp)
target_link_libraries(forge_timer_context PRIVATE forge::forge Threads::Threads)
if(FORGE_EXAMPLE_TBB_LIBRARY)
    target_link_libraries(forge_timer_context PRIVATE ${FORGE_EXAMPLE_TBB_LIBRARY})
endif()

add_executable(forge_runtime_context forge_runtime_context_example.cpp)
target_link_libraries(forge_runtime_context PRIVATE forge::forge Threads::Threads)
if(FORGE_EXAMPLE_TBB_LIBRARY)
    target_link_libraries(forge_runtime_context PRIVATE ${FORGE_EXAMPLE_TBB_LIBRARY})
endif()

add_executable(forge_async_scope forge_async_scope_example.cpp)
target_link_libraries(forge_async_scope PRIVATE forge::forge Threads::Threads)
if(FORGE_EXAMPLE_TBB_LIBRARY)
    target_link_libraries(forge_async_scope PRIVATE ${FORGE_EXAMPLE_TBB_LIBRARY})
endif()

add_executable(forge_channel forge_channel_example.cpp)
target_link_libraries(forge_channel PRIVATE forge::forge Threads::Threads)
if(FORGE_EXAMPLE_TBB_LIBRARY)
    target_link_libraries(forge_channel PRIVATE ${FORGE_EXAMPLE_TBB_LIBRARY})
endif()

add_executable(forge_resource_context forge_resource_context_example.cpp)
target_link_libraries(forge_resource_context PRIVATE forge::forge Threads::Threads)
if(FORGE_EXAMPLE_TBB_LIBRARY)
    target_link_libraries(forge_resource_context PRIVATE ${FORGE_EXAMPLE_TBB_LIBRARY})
endif()

add_executable(forge_strand forge_strand_example.cpp)
target_link_libraries(forge_strand PRIVATE forge::forge Threads::Threads)
if(FORGE_EXAMPLE_TBB_LIBRARY)
    target_link_libraries(forge_strand PRIVATE ${FORGE_EXAMPLE_TBB_LIBRARY})
endif()

add_executable(forge_graceful_shutdown forge_graceful_shutdown_example.cpp)
target_link_libraries(forge_graceful_shutdown PRIVATE forge::forge Threads::Threads)
if(FORGE_EXAMPLE_TBB_LIBRARY)
    target_link_libraries(forge_graceful_shutdown PRIVATE ${FORGE_EXAMPLE_TBB_LIBRARY})
endif()

add_executable(forge_type_erased_boundary forge_type_erased_boundary_example.cpp)
target_link_libraries(forge_type_erased_boundary PRIVATE forge::forge Threads::Threads)
if(FORGE_EXAMPLE_TBB_LIBRARY)
    target_link_libraries(forge_type_erased_boundary PRIVATE ${FORGE_EXAMPLE_TBB_LIBRARY})
endif()

if(FORGE_ENABLE_FORGE_RESOURCE_POLICY)
    add_executable(forge_resource_policy forge_resource_policy_example.cpp)
    target_link_libraries(forge_resource_policy PRIVATE forge::forge Threads::Threads)
    if(FORGE_EXAMPLE_TBB_LIBRARY)
        target_link_libraries(forge_resource_policy PRIVATE ${FORGE_EXAMPLE_TBB_LIBRARY})
    endif()

    add_executable(forge_bounded_pipeline forge_bounded_pipeline_example.cpp)
    target_link_libraries(forge_bounded_pipeline PRIVATE forge::forge Threads::Threads)
    if(FORGE_EXAMPLE_TBB_LIBRARY)
        target_link_libraries(forge_bounded_pipeline PRIVATE ${FORGE_EXAMPLE_TBB_LIBRARY})
    endif()
endif()

if(FORGE_HAS_FORGE_IO_LINUX_EPOLL_BACKEND)
    add_executable(forge_io_readiness forge_io_readiness_example.cpp)
    target_link_libraries(forge_io_readiness PRIVATE forge::forge Threads::Threads)
    if(FORGE_EXAMPLE_TBB_LIBRARY)
        target_link_libraries(forge_io_readiness PRIVATE ${FORGE_EXAMPLE_TBB_LIBRARY})
    endif()

    add_executable(forge_io_pipeline forge_io_pipeline_example.cpp)
    target_link_libraries(forge_io_pipeline PRIVATE forge::forge Threads::Threads)
    if(FORGE_EXAMPLE_TBB_LIBRARY)
        target_link_libraries(forge_io_pipeline PRIVATE ${FORGE_EXAMPLE_TBB_LIBRARY})
    endif()

    add_executable(forge_io_read_write forge_io_read_write_example.cpp)
    target_link_libraries(forge_io_read_write PRIVATE forge::forge Threads::Threads)
    if(FORGE_EXAMPLE_TBB_LIBRARY)
        target_link_libraries(forge_io_read_write PRIVATE ${FORGE_EXAMPLE_TBB_LIBRARY})
    endif()

    add_executable(forge_io_typed_error forge_io_typed_error_example.cpp)
    target_link_libraries(forge_io_typed_error PRIVATE forge::forge Threads::Threads)
    if(FORGE_EXAMPLE_TBB_LIBRARY)
        target_link_libraries(forge_io_typed_error PRIVATE ${FORGE_EXAMPLE_TBB_LIBRARY})
    endif()
endif()

if(FORGE_HAS_FORGE_IO_WINDOWS_IOCP_BACKEND)
    add_executable(forge_io_iocp forge_io_iocp_example.cpp)
    target_link_libraries(forge_io_iocp PRIVATE forge::forge Threads::Threads)
    if(FORGE_EXAMPLE_TBB_LIBRARY)
        target_link_libraries(forge_io_iocp PRIVATE ${FORGE_EXAMPLE_TBB_LIBRARY})
    endif()
endif()

if(FORGE_HAS_FORGE_ACCEL_BACKEND)
    add_executable(forge_accel_copy forge_accel_copy_example.cpp)
    target_link_libraries(forge_accel_copy PRIVATE forge::forge Threads::Threads)
    if(FORGE_EXAMPLE_TBB_LIBRARY)
        target_link_libraries(forge_accel_copy PRIVATE ${FORGE_EXAMPLE_TBB_LIBRARY})
    endif()

    add_executable(forge_accel_pipeline forge_accel_pipeline_example.cpp)
    target_link_libraries(forge_accel_pipeline PRIVATE forge::forge Threads::Threads)
    if(FORGE_EXAMPLE_TBB_LIBRARY)
        target_link_libraries(forge_accel_pipeline PRIVATE ${FORGE_EXAMPLE_TBB_LIBRARY})
    endif()

    add_executable(forge_accel_event forge_accel_event_example.cpp)
    target_link_libraries(forge_accel_event PRIVATE forge::forge Threads::Threads)
    if(FORGE_EXAMPLE_TBB_LIBRARY)
        target_link_libraries(forge_accel_event PRIVATE ${FORGE_EXAMPLE_TBB_LIBRARY})
    endif()

    add_executable(forge_accel_staging_buffer forge_accel_staging_buffer_example.cpp)
    target_link_libraries(forge_accel_staging_buffer PRIVATE forge::forge Threads::Threads)
    if(FORGE_EXAMPLE_TBB_LIBRARY)
        target_link_libraries(forge_accel_staging_buffer PRIVATE ${FORGE_EXAMPLE_TBB_LIBRARY})
    endif()

    add_executable(forge_inference_runtime_sketch forge_inference_runtime_sketch.cpp)
    target_link_libraries(forge_inference_runtime_sketch PRIVATE forge::forge Threads::Threads)
    if(FORGE_EXAMPLE_TBB_LIBRARY)
        target_link_libraries(forge_inference_runtime_sketch PRIVATE ${FORGE_EXAMPLE_TBB_LIBRARY})
    endif()

    add_executable(forge_accel_message_device forge_accel_message_device_example.cpp)
    target_link_libraries(forge_accel_message_device PRIVATE forge::forge Threads::Threads)
    if(FORGE_EXAMPLE_TBB_LIBRARY)
        target_link_libraries(forge_accel_message_device PRIVATE ${FORGE_EXAMPLE_TBB_LIBRARY})
    endif()

    add_executable(forge_accel_typed_error forge_accel_typed_error_example.cpp)
    target_link_libraries(forge_accel_typed_error PRIVATE forge::forge Threads::Threads)
    if(FORGE_EXAMPLE_TBB_LIBRARY)
        target_link_libraries(forge_accel_typed_error PRIVATE ${FORGE_EXAMPLE_TBB_LIBRARY})
    endif()
endif()
endif()

# Coroutine examples (source-level guards)
add_executable(execution_coro execution_coro_example.cpp)
target_link_libraries(execution_coro PRIVATE forge::forge)
if(FORGE_EXAMPLE_TBB_LIBRARY)
    target_link_libraries(execution_coro PRIVATE ${FORGE_EXAMPLE_TBB_LIBRARY})
endif()
target_include_directories(execution_coro BEFORE PRIVATE "${CMAKE_SOURCE_DIR}/backport")

if(FORGE_ENABLE_FORGE_RUNTIME)
add_executable(forge_task forge_task_example.cpp)
target_link_libraries(forge_task PRIVATE forge::forge)
if(FORGE_EXAMPLE_TBB_LIBRARY)
    target_link_libraries(forge_task PRIVATE ${FORGE_EXAMPLE_TBB_LIBRARY})
endif()
endif()

# linalg SIMD example (only with mdspan)
if(FORGE_EXAMPLE_HAS_MDSPAN)
    add_executable(linalg_simd_example linalg_simd_example.cpp)
    target_link_libraries(linalg_simd_example PRIVATE forge::forge)
    target_include_directories(linalg_simd_example BEFORE PRIVATE "${CMAKE_SOURCE_DIR}/backport")

    add_executable(linalg_level2 linalg_level2_example.cpp)
    target_link_libraries(linalg_level2 PRIVATE forge::forge)
    target_include_directories(linalg_level2 BEFORE PRIVATE "${CMAKE_SOURCE_DIR}/backport")

    add_executable(linalg_level3 linalg_level3_example.cpp)
    target_link_libraries(linalg_level3 PRIVATE forge::forge)
    target_include_directories(linalg_level3 BEFORE PRIVATE "${CMAKE_SOURCE_DIR}/backport")

    add_executable(linalg_views linalg_views_example.cpp)
    target_link_libraries(linalg_views PRIVATE forge::forge)
    target_include_directories(linalg_views BEFORE PRIVATE "${CMAKE_SOURCE_DIR}/backport")
endif()

set(FORGE_EXAMPLE_SMOKE_TARGETS
    unique_resource_example
    simd_example
    simd_complete_example
    execution_example
    submdspan_example
    execution_phase3_example
    execution_schedulers
    execution_let
    execution_stop_token
    execution_phase4
    forge_any_sender
    forge_any_receiver
    forge_any_scheduler
    forge_thread_pool
    forge_system_context
    forge_single_thread_context
    forge_timer_context
    forge_runtime_context
    forge_async_scope
    forge_channel
    forge_resource_context
    forge_strand
    forge_graceful_shutdown
    forge_type_erased_boundary
    forge_resource_policy
    forge_bounded_pipeline
    forge_io_readiness
    forge_io_pipeline
    forge_io_read_write
    forge_io_typed_error
    forge_io_iocp
    forge_accel_copy
    forge_accel_pipeline
    forge_accel_event
    forge_accel_staging_buffer
    forge_accel_message_device
    forge_accel_typed_error
    forge_inference_runtime_sketch
    execution_coro
    forge_task
    linalg_simd_example
    linalg_level2
    linalg_level3
    linalg_views)

foreach(target_name IN LISTS FORGE_EXAMPLE_SMOKE_TARGETS)
    if(TARGET ${target_name})
        forge_add_example_smoke(${target_name})
    endif()
endforeach()
