# CC Forge 示例索引

这个索引覆盖 `example/` 中所有会由 `example/CMakeLists.txt` 注册的公开示例。启用
`FORGE_BUILD_TESTS=ON` 与 `FORGE_BUILD_EXAMPLES=ON` 时，每个已构建示例也会注册为
`example_<target>_smoke` CTest；平台或 feature gate 不满足的示例不会构建。

## Standard backports

- [`unique_resource_example.cpp`](../example/unique_resource_example.cpp)
- [`constant_wrapper_example.cpp`](../example/constant_wrapper_example.cpp)
- [`simd_example.cpp`](../example/simd_example.cpp)
- [`simd_complete_example.cpp`](../example/simd_complete_example.cpp)
- [`submdspan_example.cpp`](../example/submdspan_example.cpp)
- [`padded_mdspan_layout_example.cpp`](../example/padded_mdspan_layout_example.cpp)
- [`linalg_simd_example.cpp`](../example/linalg_simd_example.cpp)
- [`linalg_level2_example.cpp`](../example/linalg_level2_example.cpp)
- [`linalg_level3_example.cpp`](../example/linalg_level3_example.cpp)
- [`linalg_views_example.cpp`](../example/linalg_views_example.cpp)

## Execution

- [`execution_example.cpp`](../example/execution_example.cpp)
- [`execution_phase3_example.cpp`](../example/execution_phase3_example.cpp)
- [`execution_schedulers_example.cpp`](../example/execution_schedulers_example.cpp)
- [`execution_let_example.cpp`](../example/execution_let_example.cpp)
- [`execution_stop_token_example.cpp`](../example/execution_stop_token_example.cpp)
- [`execution_spawn_future_example.cpp`](../example/execution_spawn_future_example.cpp)
- [`execution_phase4_example.cpp`](../example/execution_phase4_example.cpp)
- [`execution_on_example.cpp`](../example/execution_on_example.cpp)
- [`execution_affine_example.cpp`](../example/execution_affine_example.cpp)
- [`execution_unstoppable_example.cpp`](../example/execution_unstoppable_example.cpp)
- [`execution_coro_example.cpp`](../example/execution_coro_example.cpp)

## Forge runtime and erasure

- [`forge_any_sender_example.cpp`](../example/forge_any_sender_example.cpp)
- [`forge_any_receiver_example.cpp`](../example/forge_any_receiver_example.cpp)
- [`forge_any_scheduler_example.cpp`](../example/forge_any_scheduler_example.cpp)
- [`forge_thread_pool_example.cpp`](../example/forge_thread_pool_example.cpp)
- [`forge_system_context_example.cpp`](../example/forge_system_context_example.cpp)
- [`forge_single_thread_context_example.cpp`](../example/forge_single_thread_context_example.cpp)
- [`forge_timer_context_example.cpp`](../example/forge_timer_context_example.cpp)
- [`forge_runtime_context_example.cpp`](../example/forge_runtime_context_example.cpp)
- [`forge_async_scope_example.cpp`](../example/forge_async_scope_example.cpp)
- [`forge_channel_example.cpp`](../example/forge_channel_example.cpp)
- [`forge_resource_context_example.cpp`](../example/forge_resource_context_example.cpp)
- [`forge_strand_example.cpp`](../example/forge_strand_example.cpp)
- [`forge_graceful_shutdown_example.cpp`](../example/forge_graceful_shutdown_example.cpp)
- [`forge_type_erased_boundary_example.cpp`](../example/forge_type_erased_boundary_example.cpp)
- [`forge_task_example.cpp`](../example/forge_task_example.cpp)
- [`forge_resource_policy_example.cpp`](../example/forge_resource_policy_example.cpp)
- [`forge_bounded_pipeline_example.cpp`](../example/forge_bounded_pipeline_example.cpp)

## Coroutine-native byte IO

- [`forge_byte_vocabulary_example.cpp`](../example/forge_byte_vocabulary_example.cpp)
- [`forge_memory_stream_example.cpp`](../example/forge_memory_stream_example.cpp)
- [`forge_stream_erasure_example.cpp`](../example/forge_stream_erasure_example.cpp)
- [`forge_owned_async_stream_example.cpp`](../example/forge_owned_async_stream_example.cpp)
- [`forge_line_protocol_example.cpp`](../example/forge_line_protocol_example.cpp)
- [`forge_coro_io_example.cpp`](../example/forge_coro_io_example.cpp)
- [`forge_coro_interop_example.cpp`](../example/forge_coro_interop_example.cpp)
- [`forge_coro_combinator_example.cpp`](../example/forge_coro_combinator_example.cpp)
- [`forge_coro_timeout_example.cpp`](../example/forge_coro_timeout_example.cpp)
- [`forge_coro_executor_hop_example.cpp`](../example/forge_coro_executor_hop_example.cpp)
- [`forge_timer_await_example.cpp`](../example/forge_timer_await_example.cpp)
- [`forge_context_await_example.cpp`](../example/forge_context_await_example.cpp)
- [`forge_context_cancel_example.cpp`](../example/forge_context_cancel_example.cpp)
- [`forge_coro_line_pipeline_example.cpp`](../example/forge_coro_line_pipeline_example.cpp)
- [`forge_io_readiness_example.cpp`](../example/forge_io_readiness_example.cpp)
- [`forge_io_pipeline_example.cpp`](../example/forge_io_pipeline_example.cpp)
- [`forge_io_read_write_example.cpp`](../example/forge_io_read_write_example.cpp)
- [`forge_io_typed_error_example.cpp`](../example/forge_io_typed_error_example.cpp)
- [`forge_io_iocp_example.cpp`](../example/forge_io_iocp_example.cpp)
- [`forge_io_uring_read_write_example.cpp`](../example/forge_io_uring_read_write_example.cpp)
