// The synthetic no-mdspan fixture exercises only the linalg pre-include.
#if !defined(FORGE_TEST_LINALG_PREINCLUDE_ONLY)
#include <execution>
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <concepts>
#include <condition_variable>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <exception>
#include <functional>
#include <initializer_list>
#include <limits>
#include <list>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <new>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>
#endif

#if !defined(FORGE_NO_BASE_MDSPAN) && __has_include(<linalg>)
#include <linalg>
#endif

#if !defined(FORGE_TEST_LINALG_PREINCLUDE_ONLY)
#if defined(FORGE_TEST_D1_INCLUDE_SIMD)
#include <simd>
#endif

// Parse the standard headers first, then hide the old spellings from Forge.
// Local declarations and their uses are renamed together. A reference to an
// existing std::execution entity instead names an undeclared identifier.
#define receiver_t forge_test_hidden_receiver_t
#define sender_t forge_test_hidden_sender_t
#define operation_state_t forge_test_hidden_operation_state_t
#define scheduler_t forge_test_hidden_scheduler_t
#define sender_to forge_test_hidden_sender_to
#define receiver_of forge_test_hidden_receiver_of
#define queryable forge_test_hidden_queryable
#define make_env forge_test_hidden_make_env
#define env_domain_t forge_test_hidden_env_domain_t
#define receiver_domain_t forge_test_hidden_receiver_domain_t
#define sender_domain_t forge_test_hidden_sender_domain_t
#define read_env_t forge_test_hidden_read_env_t
#define value_types_of forge_test_hidden_value_types_of
#define error_types_of forge_test_hidden_error_types_of
#define sends_stopped_v forge_test_hidden_sends_stopped_v

#include <forge/any_receiver.hpp>
#include <forge/any_scheduler.hpp>
#include <forge/any_sender.hpp>
#include <forge/any_stop_token.hpp>
#include <forge/async_scope.hpp>
#include <forge/bulk.hpp>
#include <forge/channel.hpp>
#include <forge/erased_sender.hpp>
#include <forge/execution.hpp>
#include <forge/execution_extensions.hpp>
#include <forge/io/async_stream.hpp>
#include <forge/io/buffer.hpp>
#include <forge/io/combinators.hpp>
#if defined(FORGE_HAS_FORGE_IO_BACKEND)
#include <forge/io/context.hpp>
#endif
#include <forge/io/context_await.hpp>
#include <forge/io/coro.hpp>
#include <forge/io/env.hpp>
#include <forge/io/error.hpp>
#if defined(FORGE_HAS_FORGE_IO_URING_BACKEND)
#include <forge/io/io_uring_context.hpp>
#endif
#include <forge/io/memory_stream.hpp>
#include <forge/io/result.hpp>
#include <forge/io/stream.hpp>
#include <forge/io/timer_await.hpp>
#include <forge/io.hpp>
#include <forge/linalg.hpp>
#include <forge/resource_context.hpp>
#include <forge/resource_policy.hpp>
#include <forge/runtime_context.hpp>
#if defined(FORGE_TEST_D1_INCLUDE_SIMD)
#include <forge/simd.hpp>
#endif
#include <forge/single_thread_context.hpp>
#include <forge/start_detached.hpp>
#include <forge/static_thread_pool.hpp>
#include <forge/strand.hpp>
#include <forge/system_context.hpp>
#include <forge/task.hpp>
#include <forge/timer_context.hpp>
#include <forge/wait_result.hpp>

int main() {}
#endif
