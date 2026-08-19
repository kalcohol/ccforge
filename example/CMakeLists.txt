# Forge Examples

find_package(Threads REQUIRED)
find_library(FORGE_EXAMPLE_TBB_LIBRARY NAMES tbb)

function(forge_add_example_smoke target_name)
    if(FORGE_BUILD_TESTS)
        add_test(NAME "example_${target_name}_smoke" COMMAND $<TARGET_FILE:${target_name}>)
        set_tests_properties("example_${target_name}_smoke" PROPERTIES TIMEOUT 60)
    endif()
endfunction()

function(forge_add_example target_name source_file)
    set(_forge_example_links forge::forge)
    set(_forge_example_use_backport OFF)
    set(_forge_example_use_tbb OFF)

    foreach(_forge_example_arg IN LISTS ARGN)
        if(_forge_example_arg STREQUAL "THREADS")
            list(APPEND _forge_example_links Threads::Threads)
        elseif(_forge_example_arg STREQUAL "BACKPORT")
            set(_forge_example_use_backport ON)
        elseif(_forge_example_arg STREQUAL "TBB")
            set(_forge_example_use_tbb ON)
        else()
            message(FATAL_ERROR "Unknown forge_add_example option: ${_forge_example_arg}")
        endif()
    endforeach()

    add_executable(${target_name} ${source_file})
    target_link_libraries(${target_name} PRIVATE ${_forge_example_links})

    if(_forge_example_use_tbb AND FORGE_EXAMPLE_TBB_LIBRARY)
        target_link_libraries(${target_name} PRIVATE ${FORGE_EXAMPLE_TBB_LIBRARY})
    endif()

    if(_forge_example_use_backport)
        target_include_directories(${target_name} BEFORE PRIVATE "${CMAKE_SOURCE_DIR}/backport")
    endif()

    set_property(GLOBAL APPEND PROPERTY FORGE_EXAMPLE_SMOKE_TARGETS ${target_name})
endfunction()

forge_add_example(unique_resource_example unique_resource_example.cpp)
forge_add_example(constant_wrapper_example constant_wrapper_example.cpp BACKPORT)
forge_add_example(simd_example simd_example.cpp)
forge_add_example(simd_complete_example simd_complete_example.cpp)
forge_add_example(forge_byte_vocabulary forge_byte_vocabulary_example.cpp)
forge_add_example(forge_memory_stream forge_memory_stream_example.cpp)
forge_add_example(forge_stream_erasure forge_stream_erasure_example.cpp)
forge_add_example(forge_owned_async_stream forge_owned_async_stream_example.cpp THREADS TBB)
target_sources(
    forge_owned_async_stream
    PRIVATE forge_owned_async_stream_protocol.cc)
forge_add_example(forge_line_protocol forge_line_protocol_example.cpp)
forge_add_example(forge_coro_io forge_coro_io_example.cpp THREADS TBB)
forge_add_example(forge_coro_interop forge_coro_interop_example.cpp THREADS TBB)
forge_add_example(forge_coro_combinator forge_coro_combinator_example.cpp THREADS TBB)
forge_add_example(forge_coro_timeout forge_coro_timeout_example.cpp THREADS TBB)
forge_add_example(forge_coro_executor_hop forge_coro_executor_hop_example.cpp THREADS TBB)
forge_add_example(forge_timer_await forge_timer_await_example.cpp THREADS TBB)
forge_add_example(forge_coro_line_pipeline forge_coro_line_pipeline_example.cpp THREADS TBB)

if(FORGE_HAS_FORGE_IO_LINUX_EPOLL_BACKEND)
    forge_add_example(forge_context_await forge_context_await_example.cpp THREADS TBB)
    forge_add_example(forge_context_cancel forge_context_cancel_example.cpp THREADS TBB)
endif()

# std::execution examples are built unconditionally to ensure wrapper injection
# stays functional.
forge_add_example(execution_example execution_example.cpp BACKPORT TBB)
forge_add_example(execution_phase3_example execution_phase3_example.cpp BACKPORT TBB)
forge_add_example(execution_schedulers execution_schedulers_example.cpp BACKPORT TBB)
forge_add_example(execution_let execution_let_example.cpp BACKPORT TBB)
forge_add_example(execution_stop_token execution_stop_token_example.cpp BACKPORT TBB)
forge_add_example(execution_spawn_future execution_spawn_future_example.cpp BACKPORT TBB)
forge_add_example(execution_phase4 execution_phase4_example.cpp BACKPORT TBB)
forge_add_example(execution_on execution_on_example.cpp BACKPORT TBB)
forge_add_example(execution_affine execution_affine_example.cpp BACKPORT TBB)
forge_add_example(execution_unstoppable execution_unstoppable_example.cpp BACKPORT TBB)
forge_add_example(execution_coro execution_coro_example.cpp BACKPORT TBB)

# std::submdspan/linalg examples need <mdspan>; older libstdc++ releases do not
# provide it, so keep them guarded.
include(CheckCXXSourceCompiles)
unset(FORGE_EXAMPLE_HAS_MDSPAN CACHE)
unset(FORGE_EXAMPLE_HAS_MDSPAN)
check_cxx_source_compiles("
    #include <mdspan>
    int main() { return 0; }
" FORGE_EXAMPLE_HAS_MDSPAN)

if(FORGE_EXAMPLE_HAS_MDSPAN)
    forge_add_example(submdspan_example submdspan_example.cpp BACKPORT)
    forge_add_example(padded_mdspan_layout_example padded_mdspan_layout_example.cpp BACKPORT)
    forge_add_example(linalg_simd_example linalg_simd_example.cpp BACKPORT)
    forge_add_example(linalg_level2 linalg_level2_example.cpp BACKPORT)
    forge_add_example(linalg_level3 linalg_level3_example.cpp BACKPORT)
    forge_add_example(linalg_views linalg_views_example.cpp BACKPORT)
endif()

if(FORGE_ENABLE_FORGE_RUNTIME)
    foreach(_forge_example_target IN ITEMS
            forge_any_sender
            forge_any_receiver)
        forge_add_example(${_forge_example_target} ${_forge_example_target}_example.cpp TBB)
    endforeach()

    foreach(_forge_example_target IN ITEMS
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
            forge_type_erased_boundary)
        forge_add_example(${_forge_example_target} ${_forge_example_target}_example.cpp THREADS TBB)
    endforeach()

    forge_add_example(forge_task forge_task_example.cpp TBB)

    if(FORGE_ENABLE_FORGE_RESOURCE_POLICY)
        forge_add_example(forge_resource_policy forge_resource_policy_example.cpp THREADS TBB)
        forge_add_example(forge_bounded_pipeline forge_bounded_pipeline_example.cpp THREADS TBB)
    endif()

    if(FORGE_HAS_FORGE_IO_LINUX_EPOLL_BACKEND)
        foreach(_forge_example_target IN ITEMS
                forge_io_readiness
                forge_io_pipeline
                forge_io_read_write
                forge_io_typed_error)
            forge_add_example(${_forge_example_target} ${_forge_example_target}_example.cpp THREADS TBB)
        endforeach()
    endif()

    if(FORGE_HAS_FORGE_IO_WINDOWS_IOCP_BACKEND)
        forge_add_example(forge_io_iocp forge_io_iocp_example.cpp THREADS TBB)
    endif()

    if(FORGE_HAS_FORGE_IO_URING_BACKEND)
        forge_add_example(forge_io_uring_read_write forge_io_uring_read_write_example.cpp THREADS TBB)
    endif()

endif()

get_property(FORGE_EXAMPLE_SMOKE_TARGETS GLOBAL PROPERTY FORGE_EXAMPLE_SMOKE_TARGETS)
foreach(target_name IN LISTS FORGE_EXAMPLE_SMOKE_TARGETS)
    forge_add_example_smoke(${target_name})
endforeach()
