# CC Forge 示例索引

启用 `FORGE_BUILD_EXAMPLES=ON` 构建；另启用 `FORGE_BUILD_TESTS=ON` 注册 `example_<target>_smoke`。明确不可用返回 77 并由 CTest 标为跳过。标准接口按“强制 OR 完整 OR 非部分”门控；NATIVE_INCOMPATIBLE 列出依赖扩展的例外。

## 标准 backport

| 文件 | 演示内容 | target / 冒烟测试 | 门控 | 文档页 |
| --- | --- | --- | --- | --- |
| [constant_wrapper_example.cpp](../example/constant_wrapper_example.cpp) | constant wrapper 的接口组合与完成结果 | `constant_wrapper_example` / `example_constant_wrapper_example_smoke` | CONSTANT_WRAPPER：强制 OR 完整 OR 非部分 | [说明](backports/mdspan.md) |
| [hive_capacity_limits_example.cpp](../example/hive_capacity_limits_example.cpp) | hive capacity limits 的接口组合与完成结果 | `hive_capacity_limits` / `example_hive_capacity_limits_smoke` | HIVE：强制 OR 完整 OR 非部分 | [说明](backports/hive.md) |
| [hive_example.cpp](../example/hive_example.cpp) | hive 的接口组合与完成结果 | `hive_example` / `example_hive_example_smoke` | HIVE：强制 OR 完整 OR 非部分 | [说明](backports/hive.md) |
| [hive_get_iterator_example.cpp](../example/hive_get_iterator_example.cpp) | hive get iterator 的接口组合与完成结果 | `hive_get_iterator` / `example_hive_get_iterator_smoke` | HIVE：强制 OR 完整 OR 非部分 | [说明](backports/hive.md) |
| [hive_sort_unique_example.cpp](../example/hive_sort_unique_example.cpp) | hive sort unique 的接口组合与完成结果 | `hive_sort_unique` / `example_hive_sort_unique_smoke` | HIVE：强制 OR 完整 OR 非部分 | [说明](backports/hive.md) |
| [hive_splice_example.cpp](../example/hive_splice_example.cpp) | hive splice 的接口组合与完成结果 | `hive_splice` / `example_hive_splice_smoke` | HIVE：强制 OR 完整 OR 非部分 | [说明](backports/hive.md) |
| [hive_stable_handles_example.cpp](../example/hive_stable_handles_example.cpp) | hive stable handles 的接口组合与完成结果 | `hive_stable_handles` / `example_hive_stable_handles_smoke` | HIVE：强制 OR 完整 OR 非部分 | [说明](backports/hive.md) |
| [linalg_level2_example.cpp](../example/linalg_level2_example.cpp) | linalg level2 的接口组合与完成结果 | `linalg_level2` / `example_linalg_level2_smoke` | LINALG：强制 OR 完整 OR 非部分；需要 mdspan | [说明](backports/linalg.md) |
| [linalg_level3_example.cpp](../example/linalg_level3_example.cpp) | linalg level3 的接口组合与完成结果 | `linalg_level3` / `example_linalg_level3_smoke` | LINALG：强制 OR 完整 OR 非部分；需要 mdspan | [说明](backports/linalg.md) |
| [linalg_simd_example.cpp](../example/linalg_simd_example.cpp) | linalg simd 的接口组合与完成结果 | `linalg_simd_example` / `example_linalg_simd_example_smoke` | LINALG：强制 OR 完整 OR 非部分；需要 mdspan | [说明](backports/linalg.md) |
| [linalg_views_example.cpp](../example/linalg_views_example.cpp) | linalg views 的接口组合与完成结果 | `linalg_views` / `example_linalg_views_smoke` | LINALG：强制 OR 完整 OR 非部分；需要 mdspan | [说明](backports/linalg.md) |
| [padded_mdspan_layout_example.cpp](../example/padded_mdspan_layout_example.cpp) | padded mdspan layout 的接口组合与完成结果 | `padded_mdspan_layout_example` / `example_padded_mdspan_layout_example_smoke` | MDSPAN_PADDED_LAYOUTS：强制 OR 完整 OR 非部分；需要 mdspan | [说明](backports/mdspan.md) |
| [simd_complete_example.cpp](../example/simd_complete_example.cpp) | SIMD 内存、mask 与排列（Forge 扩展拼写） | `simd_complete_example` / `example_simd_complete_example_smoke` | SIMD：强制 OR 完整 OR 非部分；NATIVE_INCOMPATIBLE，Forge 扩展 | [说明](backports/simd.md) |
| [simd_example.cpp](../example/simd_example.cpp) | simd 的接口组合与完成结果 | `simd_example` / `example_simd_example_smoke` | SIMD：强制 OR 完整 OR 非部分 | [说明](backports/simd.md) |
| [submdspan_example.cpp](../example/submdspan_example.cpp) | 行列、步长切片与边界单元 | `submdspan_example` / `example_submdspan_example_smoke` | SUBMDSPAN：强制 OR 完整 OR 非部分；需要 mdspan | [说明](backports/mdspan.md) |
| [unique_resource_example.cpp](../example/unique_resource_example.cpp) | unique resource 的接口组合与完成结果 | `unique_resource_example` / `example_unique_resource_example_smoke` | 始终 | [说明](backports/unique_resource.md) |

## Execution

| 文件 | 演示内容 | target / 冒烟测试 | 门控 | 文档页 |
| --- | --- | --- | --- | --- |
| [execution_affine_example.cpp](../example/execution_affine_example.cpp) | execution affine 的接口组合与完成结果 | `execution_affine` / `example_execution_affine_smoke` | SENDERS：强制 OR 完整 OR 非部分 | [说明](backports/execution.md) |
| [execution_phase3_example.cpp](../example/execution_phase3_example.cpp) | when_all、结果复用、bulk 与 scope spawn（Forge 扩展拼写） | `execution_phase3_example` / `example_execution_phase3_example_smoke` | SENDERS：强制 OR 完整 OR 非部分；NATIVE_INCOMPATIBLE，Forge 扩展 | [说明](backports/execution.md) |
| [execution_coro_example.cpp](../example/execution_coro_example.cpp) | execution coro 的接口组合与完成结果 | `execution_coro` / `example_execution_coro_smoke` | SENDERS：强制 OR 完整 OR 非部分 | [说明](backports/execution.md) |
| [execution_example.cpp](../example/execution_example.cpp) | execution 的接口组合与完成结果 | `execution_example` / `example_execution_example_smoke` | SENDERS：强制 OR 完整 OR 非部分 | [说明](backports/execution.md) |
| [execution_let_example.cpp](../example/execution_let_example.cpp) | execution let 的接口组合与完成结果 | `execution_let` / `example_execution_let_smoke` | SENDERS：强制 OR 完整 OR 非部分 | [说明](backports/execution.md) |
| [execution_on_example.cpp](../example/execution_on_example.cpp) | execution on 的接口组合与完成结果 | `execution_on` / `example_execution_on_smoke` | SENDERS：强制 OR 完整 OR 非部分 | [说明](backports/execution.md) |
| [execution_schedulers_example.cpp](../example/execution_schedulers_example.cpp) | execution schedulers 的接口组合与完成结果 | `execution_schedulers` / `example_execution_schedulers_smoke` | SENDERS：强制 OR 完整 OR 非部分 | [说明](backports/execution.md) |
| [execution_phase4_example.cpp](../example/execution_phase4_example.cpp) | scope spawn 与异步 join | `execution_phase4` / `example_execution_phase4_smoke` | SENDERS：强制 OR 完整 OR 非部分 | [说明](backports/execution.md) |
| [execution_spawn_future_example.cpp](../example/execution_spawn_future_example.cpp) | execution spawn future 的接口组合与完成结果 | `execution_spawn_future` / `example_execution_spawn_future_smoke` | SENDERS：强制 OR 完整 OR 非部分 | [说明](backports/execution.md) |
| [execution_stop_token_example.cpp](../example/execution_stop_token_example.cpp) | execution stop token 的接口组合与完成结果 | `execution_stop_token` / `example_execution_stop_token_smoke` | SENDERS：强制 OR 完整 OR 非部分 | [说明](backports/execution.md) |
| [execution_unstoppable_example.cpp](../example/execution_unstoppable_example.cpp) | execution unstoppable 的接口组合与完成结果 | `execution_unstoppable` / `example_execution_unstoppable_smoke` | SENDERS：强制 OR 完整 OR 非部分 | [说明](backports/execution.md) |

## Forge runtime 与擦除

| 文件 | 演示内容 | target / 冒烟测试 | 门控 | 文档页 |
| --- | --- | --- | --- | --- |
| [forge_any_receiver_example.cpp](../example/forge_any_receiver_example.cpp) | forge any receiver 的接口组合与完成结果 | `forge_any_receiver` / `example_forge_any_receiver_smoke` | FORGE_ENABLE_FORGE_RUNTIME | [说明](forge-utilities.md) |
| [forge_any_scheduler_example.cpp](../example/forge_any_scheduler_example.cpp) | forge any scheduler 的接口组合与完成结果 | `forge_any_scheduler` / `example_forge_any_scheduler_smoke` | FORGE_ENABLE_FORGE_RUNTIME | [说明](forge-utilities.md) |
| [forge_any_sender_example.cpp](../example/forge_any_sender_example.cpp) | forge any sender 的接口组合与完成结果 | `forge_any_sender` / `example_forge_any_sender_smoke` | FORGE_ENABLE_FORGE_RUNTIME | [说明](forge-utilities.md) |
| [forge_async_scope_example.cpp](../example/forge_async_scope_example.cpp) | forge async scope 的接口组合与完成结果 | `forge_async_scope` / `example_forge_async_scope_smoke` | FORGE_ENABLE_FORGE_RUNTIME | [说明](forge-utilities.md) |
| [forge_bounded_pipeline_example.cpp](../example/forge_bounded_pipeline_example.cpp) | 单 worker 非阻塞通道消费 | `forge_bounded_pipeline` / `example_forge_bounded_pipeline_smoke` | FORGE_ENABLE_FORGE_RUNTIME + FORGE_ENABLE_FORGE_RESOURCE_POLICY | [说明](forge-utilities.md) |
| [forge_channel_example.cpp](../example/forge_channel_example.cpp) | forge channel 的接口组合与完成结果 | `forge_channel` / `example_forge_channel_smoke` | FORGE_ENABLE_FORGE_RUNTIME | [说明](forge-utilities.md) |
| [forge_graceful_shutdown_example.cpp](../example/forge_graceful_shutdown_example.cpp) | forge graceful shutdown 的接口组合与完成结果 | `forge_graceful_shutdown` / `example_forge_graceful_shutdown_smoke` | FORGE_ENABLE_FORGE_RUNTIME | [说明](forge-utilities.md) |
| [forge_resource_context_example.cpp](../example/forge_resource_context_example.cpp) | forge resource context 的接口组合与完成结果 | `forge_resource_context` / `example_forge_resource_context_smoke` | FORGE_ENABLE_FORGE_RUNTIME | [说明](forge-utilities.md) |
| [forge_resource_policy_example.cpp](../example/forge_resource_policy_example.cpp) | forge resource policy 的接口组合与完成结果 | `forge_resource_policy` / `example_forge_resource_policy_smoke` | FORGE_ENABLE_FORGE_RUNTIME + FORGE_ENABLE_FORGE_RESOURCE_POLICY | [说明](forge-utilities.md) |
| [forge_runtime_context_example.cpp](../example/forge_runtime_context_example.cpp) | forge runtime context 的接口组合与完成结果 | `forge_runtime_context` / `example_forge_runtime_context_smoke` | FORGE_ENABLE_FORGE_RUNTIME | [说明](forge-utilities.md) |
| [forge_single_thread_context_example.cpp](../example/forge_single_thread_context_example.cpp) | forge single thread context 的接口组合与完成结果 | `forge_single_thread_context` / `example_forge_single_thread_context_smoke` | FORGE_ENABLE_FORGE_RUNTIME | [说明](forge-utilities.md) |
| [forge_strand_example.cpp](../example/forge_strand_example.cpp) | forge strand 的接口组合与完成结果 | `forge_strand` / `example_forge_strand_smoke` | FORGE_ENABLE_FORGE_RUNTIME | [说明](forge-utilities.md) |
| [forge_system_context_example.cpp](../example/forge_system_context_example.cpp) | forge system context 的接口组合与完成结果 | `forge_system_context` / `example_forge_system_context_smoke` | FORGE_ENABLE_FORGE_RUNTIME | [说明](forge-utilities.md) |
| [forge_task_example.cpp](../example/forge_task_example.cpp) | forge task 的接口组合与完成结果 | `forge_task` / `example_forge_task_smoke` | FORGE_ENABLE_FORGE_RUNTIME | [说明](forge-utilities.md) |
| [forge_thread_pool_example.cpp](../example/forge_thread_pool_example.cpp) | forge thread pool 的接口组合与完成结果 | `forge_thread_pool` / `example_forge_thread_pool_smoke` | FORGE_ENABLE_FORGE_RUNTIME | [说明](forge-utilities.md) |
| [forge_timer_context_example.cpp](../example/forge_timer_context_example.cpp) | forge timer context 的接口组合与完成结果 | `forge_timer_context` / `example_forge_timer_context_smoke` | FORGE_ENABLE_FORGE_RUNTIME | [说明](forge-utilities.md) |
| [forge_type_erased_boundary_example.cpp](../example/forge_type_erased_boundary_example.cpp) | forge type erased boundary 的接口组合与完成结果 | `forge_type_erased_boundary` / `example_forge_type_erased_boundary_smoke` | FORGE_ENABLE_FORGE_RUNTIME | [说明](forge-utilities.md) |

## OS IO sender 后端

| 文件 | 演示内容 | target / 冒烟测试 | 门控 | 文档页 |
| --- | --- | --- | --- | --- |
| [forge_io_iocp_example.cpp](../example/forge_io_iocp_example.cpp) | forge io iocp 的接口组合与完成结果 | `forge_io_iocp` / `example_forge_io_iocp_smoke` | FORGE_ENABLE_FORGE_RUNTIME + IOCP backend | [说明](forge-io.md) |
| [forge_io_pipeline_example.cpp](../example/forge_io_pipeline_example.cpp) | forge io pipeline 的接口组合与完成结果 | `forge_io_pipeline` / `example_forge_io_pipeline_smoke` | FORGE_ENABLE_FORGE_RUNTIME + epoll backend | [说明](forge-io.md) |
| [forge_io_read_write_example.cpp](../example/forge_io_read_write_example.cpp) | forge io read write 的接口组合与完成结果 | `forge_io_read_write` / `example_forge_io_read_write_smoke` | FORGE_ENABLE_FORGE_RUNTIME + epoll backend | [说明](forge-io.md) |
| [forge_io_readiness_example.cpp](../example/forge_io_readiness_example.cpp) | forge io readiness 的接口组合与完成结果 | `forge_io_readiness` / `example_forge_io_readiness_smoke` | FORGE_ENABLE_FORGE_RUNTIME + epoll backend | [说明](forge-io.md) |
| [forge_io_typed_error_example.cpp](../example/forge_io_typed_error_example.cpp) | forge io typed error 的接口组合与完成结果 | `forge_io_typed_error` / `example_forge_io_typed_error_smoke` | FORGE_ENABLE_FORGE_RUNTIME + epoll backend | [说明](forge-io.md) |
| [forge_io_uring_read_write_example.cpp](../example/forge_io_uring_read_write_example.cpp) | forge io uring read write 的接口组合与完成结果 | `forge_io_uring_read_write` / `example_forge_io_uring_read_write_smoke` | FORGE_ENABLE_FORGE_RUNTIME + io_uring backend | [说明](forge-io.md) |

## 协程 byte IO

| 文件 | 演示内容 | target / 冒烟测试 | 门控 | 文档页 |
| --- | --- | --- | --- | --- |
| [forge_byte_vocabulary_example.cpp](../example/forge_byte_vocabulary_example.cpp) | forge byte vocabulary 的接口组合与完成结果 | `forge_byte_vocabulary` / `example_forge_byte_vocabulary_smoke` | 始终 | [说明](forge-io.md) |
| [forge_context_await_example.cpp](../example/forge_context_await_example.cpp) | forge context await 的接口组合与完成结果 | `forge_context_await` / `example_forge_context_await_smoke` | epoll backend | [说明](forge-io.md) |
| [forge_context_cancel_example.cpp](../example/forge_context_cancel_example.cpp) | forge context cancel 的接口组合与完成结果 | `forge_context_cancel` / `example_forge_context_cancel_smoke` | epoll backend | [说明](forge-io.md) |
| [forge_coro_combinator_example.cpp](../example/forge_coro_combinator_example.cpp) | forge coro combinator 的接口组合与完成结果 | `forge_coro_combinator` / `example_forge_coro_combinator_smoke` | 始终 | [说明](forge-io.md) |
| [forge_coro_executor_hop_example.cpp](../example/forge_coro_executor_hop_example.cpp) | forge coro executor hop 的接口组合与完成结果 | `forge_coro_executor_hop` / `example_forge_coro_executor_hop_smoke` | 始终 | [说明](forge-io.md) |
| [forge_coro_interop_example.cpp](../example/forge_coro_interop_example.cpp) | forge coro interop 的接口组合与完成结果 | `forge_coro_interop` / `example_forge_coro_interop_smoke` | 始终 | [说明](forge-io.md) |
| [forge_coro_io_example.cpp](../example/forge_coro_io_example.cpp) | forge coro io 的接口组合与完成结果 | `forge_coro_io` / `example_forge_coro_io_smoke` | 始终 | [说明](forge-io.md) |
| [forge_coro_line_pipeline_example.cpp](../example/forge_coro_line_pipeline_example.cpp) | forge coro line pipeline 的接口组合与完成结果 | `forge_coro_line_pipeline` / `example_forge_coro_line_pipeline_smoke` | 始终 | [说明](forge-io.md) |
| [forge_coro_timeout_example.cpp](../example/forge_coro_timeout_example.cpp) | forge coro timeout 的接口组合与完成结果 | `forge_coro_timeout` / `example_forge_coro_timeout_smoke` | 始终 | [说明](forge-io.md) |
| [forge_line_protocol_example.cpp](../example/forge_line_protocol_example.cpp) | forge line protocol 的接口组合与完成结果 | `forge_line_protocol` / `example_forge_line_protocol_smoke` | 始终 | [说明](forge-io.md) |
| [forge_memory_stream_example.cpp](../example/forge_memory_stream_example.cpp) | forge memory stream 的接口组合与完成结果 | `forge_memory_stream` / `example_forge_memory_stream_smoke` | 始终 | [说明](forge-io.md) |
| [forge_owned_async_stream_example.cpp](../example/forge_owned_async_stream_example.cpp) | forge owned async stream 的接口组合与完成结果 | `forge_owned_async_stream` / `example_forge_owned_async_stream_smoke` | 始终 | [说明](forge-io.md) |
| [forge_owned_async_stream_protocol.cc](../example/forge_owned_async_stream_protocol.cc) | forge owned async stream protocol 的接口组合与完成结果 | `forge_owned_async_stream` / `example_forge_owned_async_stream_smoke` | 始终 | [说明](forge-io.md) |
| [forge_stream_erasure_example.cpp](../example/forge_stream_erasure_example.cpp) | forge stream erasure 的接口组合与完成结果 | `forge_stream_erasure` / `example_forge_stream_erasure_smoke` | 始终 | [说明](forge-io.md) |
| [forge_timer_await_example.cpp](../example/forge_timer_await_example.cpp) | forge timer await 的接口组合与完成结果 | `forge_timer_await` / `example_forge_timer_await_smoke` | 始终 | [说明](forge-io.md) |
