# Forge Examples

find_package(Threads REQUIRED)
find_library(FORGE_EXAMPLE_TBB_LIBRARY NAMES tbb)

function(forge_add_example_smoke target_name)
    if(FORGE_BUILD_TESTS)
        add_test(NAME "example_${target_name}_smoke" COMMAND $<TARGET_FILE:${target_name}>)
        set_tests_properties("example_${target_name}_smoke" PROPERTIES TIMEOUT 60)
        get_target_property(_skip77 ${target_name} FORGE_EXAMPLE_SKIP77)
        if(_skip77)
            set_tests_properties("example_${target_name}_smoke" PROPERTIES SKIP_RETURN_CODE 77)
        endif()
    endif()
endfunction()

function(forge_add_example target_name source_file)
    set(_forge_example_links forge::forge)
    set(_forge_example_skip77 OFF)
    set(_forge_example_use_tbb OFF)

    foreach(_forge_example_arg IN LISTS ARGN)
        if(_forge_example_arg STREQUAL "THREADS")
            list(APPEND _forge_example_links Threads::Threads)
        elseif(_forge_example_arg STREQUAL "SKIP77")
            set(_forge_example_skip77 ON)
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

    if(_forge_example_skip77)
        set_property(TARGET ${target_name} PROPERTY FORGE_EXAMPLE_SKIP77 ON)
    endif()

    set_property(GLOBAL APPEND PROPERTY FORGE_EXAMPLE_SMOKE_TARGETS ${target_name})
endfunction()

# Explicit exceptions depend on Forge extensions even with complete native support.
set(FORGE_EXAMPLE_NATIVE_INCOMPATIBLE execution_combinators_example simd_complete_example)
function(forge_example_standard_gate suffix output)
    if(FORGE_FORCE_${suffix}_BACKPORT OR FORGE_${suffix}_FULL OR NOT FORGE_${suffix}_PARTIAL)
        set(${output} ON PARENT_SCOPE)
    else()
        set(${output} OFF PARENT_SCOPE)
    endif()
endfunction()
function(forge_add_standard_example suffix target source)
    forge_example_standard_gate(${suffix} _available)
    if(target IN_LIST FORGE_EXAMPLE_NATIVE_INCOMPATIBLE)
        if(FORGE_${suffix}_FULL OR FORGE_${suffix}_PARTIAL)
            if(NOT FORGE_FORCE_${suffix}_BACKPORT)
                message(STATUS "Skipping ${target}: NATIVE_INCOMPATIBLE (Forge extension spelling)")
                return()
            endif()
        endif()
    endif()
    if(_available)
        forge_add_example(${target} ${source} ${ARGN})
    endif()
endfunction()

forge_add_example(unique_resource_example unique_resource_example.cpp)
forge_add_standard_example(HIVE hive_example hive_example.cpp SKIP77)
forge_add_standard_example(HIVE hive_stable_handles hive_stable_handles_example.cpp SKIP77)
forge_add_standard_example(HIVE hive_capacity_limits hive_capacity_limits_example.cpp SKIP77)
forge_add_standard_example(HIVE hive_sort_unique hive_sort_unique_example.cpp SKIP77)
forge_add_standard_example(HIVE hive_get_iterator hive_get_iterator_example.cpp SKIP77)
forge_add_standard_example(HIVE hive_splice hive_splice_example.cpp SKIP77)
forge_add_standard_example(CONSTANT_WRAPPER constant_wrapper_example constant_wrapper_example.cpp SKIP77)
forge_add_standard_example(SIMD simd_example simd_example.cpp SKIP77)
forge_add_standard_example(SIMD simd_complete_example simd_complete_example.cpp SKIP77)
forge_add_example(forge_byte_vocabulary forge_byte_vocabulary_example.cpp SKIP77)
forge_add_example(forge_memory_stream forge_memory_stream_example.cpp SKIP77)
forge_add_example(forge_stream_erasure forge_stream_erasure_example.cpp SKIP77)
forge_add_example(forge_owned_async_stream forge_owned_async_stream_example.cpp THREADS TBB SKIP77)
target_sources(
    forge_owned_async_stream
    PRIVATE forge_owned_async_stream_protocol.cc)
forge_add_example(forge_line_protocol forge_line_protocol_example.cpp SKIP77)
forge_add_example(forge_coro_io forge_coro_io_example.cpp THREADS TBB SKIP77)
forge_add_example(forge_coro_interop forge_coro_interop_example.cpp THREADS TBB SKIP77)
forge_add_example(forge_coro_combinator forge_coro_combinator_example.cpp THREADS TBB SKIP77)
forge_add_example(forge_coro_timeout forge_coro_timeout_example.cpp THREADS TBB SKIP77)
forge_add_example(forge_coro_executor_hop forge_coro_executor_hop_example.cpp THREADS TBB SKIP77)
forge_add_example(forge_timer_await forge_timer_await_example.cpp THREADS TBB SKIP77)
forge_add_example(forge_coro_line_pipeline forge_coro_line_pipeline_example.cpp THREADS TBB SKIP77)

if(FORGE_HAS_FORGE_IO_LINUX_EPOLL_BACKEND)
    forge_add_example(forge_context_await forge_context_await_example.cpp THREADS TBB SKIP77)
    forge_add_example(forge_context_cancel forge_context_cancel_example.cpp THREADS TBB SKIP77)
endif()

# Standard examples share the same three-state gate as downstream consumers.
# Native-compatible execution sources use <execution> and current protocol tags;
# the combinators example remains excluded because it demonstrates extensions.
forge_add_standard_example(SENDERS execution_example execution_example.cpp TBB SKIP77)
forge_add_standard_example(SENDERS execution_combinators_example execution_combinators_example.cpp TBB SKIP77)
forge_add_standard_example(SENDERS execution_schedulers execution_schedulers_example.cpp TBB SKIP77)
forge_add_standard_example(SENDERS execution_let execution_let_example.cpp TBB SKIP77)
forge_add_standard_example(SENDERS execution_stop_token execution_stop_token_example.cpp TBB SKIP77)
forge_add_standard_example(SENDERS execution_spawn_future execution_spawn_future_example.cpp TBB SKIP77)
forge_add_standard_example(SENDERS execution_scope execution_scope_example.cpp TBB SKIP77)
forge_add_standard_example(SENDERS execution_on execution_on_example.cpp TBB SKIP77)
forge_add_standard_example(SENDERS execution_affine execution_affine_example.cpp TBB SKIP77)
forge_add_standard_example(SENDERS execution_unstoppable execution_unstoppable_example.cpp TBB SKIP77)
forge_add_standard_example(SENDERS execution_coro execution_coro_example.cpp TBB SKIP77)

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
    forge_add_standard_example(SUBMDSPAN submdspan_example submdspan_example.cpp SKIP77)
    forge_add_standard_example(MDSPAN_PADDED_LAYOUTS padded_mdspan_layout_example padded_mdspan_layout_example.cpp SKIP77)
    forge_add_standard_example(LINALG linalg_simd_example linalg_simd_example.cpp SKIP77)
    forge_add_standard_example(LINALG linalg_level2 linalg_level2_example.cpp SKIP77)
    forge_add_standard_example(LINALG linalg_level3 linalg_level3_example.cpp SKIP77)
    forge_add_standard_example(LINALG linalg_views linalg_views_example.cpp SKIP77)
endif()

if(FORGE_ENABLE_FORGE_RUNTIME)
    foreach(_forge_example_target IN ITEMS
            forge_any_sender
            forge_any_receiver)
        forge_add_example(${_forge_example_target} ${_forge_example_target}_example.cpp TBB SKIP77)
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
        forge_add_example(${_forge_example_target} ${_forge_example_target}_example.cpp THREADS TBB SKIP77)
    endforeach()

    forge_add_example(forge_task forge_task_example.cpp TBB SKIP77)

    if(FORGE_ENABLE_FORGE_RESOURCE_POLICY)
        forge_add_example(forge_resource_policy forge_resource_policy_example.cpp THREADS TBB SKIP77)
        forge_add_example(forge_bounded_pipeline forge_bounded_pipeline_example.cpp THREADS TBB SKIP77)
    endif()

    if(FORGE_HAS_FORGE_IO_LINUX_EPOLL_BACKEND)
        foreach(_forge_example_target IN ITEMS
                forge_io_readiness
                forge_io_pipeline
                forge_io_read_write
                forge_io_typed_error)
            forge_add_example(${_forge_example_target} ${_forge_example_target}_example.cpp THREADS TBB SKIP77)
        endforeach()
    endif()

    if(FORGE_HAS_FORGE_IO_WINDOWS_IOCP_BACKEND)
        forge_add_example(forge_io_iocp forge_io_iocp_example.cpp THREADS TBB SKIP77)
    endif()

    if(FORGE_HAS_FORGE_IO_URING_BACKEND)
        forge_add_example(forge_io_uring_read_write forge_io_uring_read_write_example.cpp THREADS TBB SKIP77)
    endif()

endif()

get_property(FORGE_EXAMPLE_SMOKE_TARGETS GLOBAL PROPERTY FORGE_EXAMPLE_SMOKE_TARGETS)
foreach(target_name IN LISTS FORGE_EXAMPLE_SMOKE_TARGETS)
    forge_add_example_smoke(${target_name})
endforeach()
