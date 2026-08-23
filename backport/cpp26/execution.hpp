// MIT License
//
// Copyright (c) 2026 CC Forge Project
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

// NOTE: This is a Forge backport of a focused P2300 sender/receiver subset.
// It is intentionally correctness-first and leaves large conformance gaps
// documented below instead of papering over them.
//
// IMPLEMENTATION STATUS (Forge C++26 execution backport - Phase 1-4 subset):
//
// IMPLEMENTED:
//   Sender factories : just, just_error, just_stopped, read_env
//   Value adaptors   : then, upon_error, upon_stopped
//   Sender adaptors  : let_value, let_error, let_stopped, write_env
//   Scheduler ops    : starts_on, schedule_from, continues_on, on, affine,
//                      bulk, bulk_chunked, bulk_unchunked (serial subset)
//   Forge extensions : transfer_just, split
//   Combinators      : into_variant, when_all, when_all_with_variant, split,
//                      associate, spawn, spawn_future
//   Consumers        : sync_wait, sync_wait_with_variant (via this_thread)
//   Stopped utils    : stopped_as_optional, stopped_as_error, unstoppable
//   Schedulers       : inline_scheduler, run_loop (mutex+cv)
//   Stop tokens      : inplace_stop_source/token/callback, never_stop_token,
//                      stoppable_token concepts
//   Coroutine bridge : as_awaitable, with_awaitable_senders (C++20 coroutines)
//   Infra            : completion_signatures_of_t,
//                      value_types_of_t, error_types_of_t, sends_stopped,
//                      transform_completion_signatures, enable_sender,
//                      get_start_scheduler/get_delegation_scheduler,
//                      forwarding_query, get_forward_progress_guarantee,
//                      get_completion_scheduler/get_completion_domain CPOs,
//                      get_await_completion_adaptor CPO,
//                      transform_sender/apply_sender domain dispatch,
//                      CPO member-function-first dispatch
//   Verification     : execution subset covered by dedicated ThreadSanitizer
//                      and ASan+UBSan container configurations.
//
// DEVIATIONS from current working draft [exec]:
//   - CPO customization is member-first for Forge-provided senders/receivers,
//     with tag_invoke fallback retained for existing custom types.
//     One-argument environment and scheduler queries are also member-query-
//     first, with tag_invoke fallback.
//   - sync_wait computes result types against its run_loop environment
//     (sync_wait_env: get_scheduler / get_start_scheduler /
//     get_delegation_scheduler). As an extension it also accepts senders
//     with zero or multiple value-completion shapes: zero value alternatives
//     yield tuple<> and multiple alternatives yield variant<tuple<...>, ...>,
//     where the working draft mandates exactly one value completion
//     signature.
//   - as_awaitable produces the working-draft result shape (void / T /
//     tuple<...> for zero / one / many values of the single value-completion
//     signature). Its dispatch honors direct/transformed as_awaitable members,
//     ordinary awaiters (including member/free operator co_await), and the
//     await-completion adaptor before constructing the sender bridge. Senders
//     with multiple value-completion alternatives fall back to the original
//     expression and therefore do not become awaitable through this bridge.
//   - The receiver concept additionally requires
//     is_nothrow_move_constructible_v of the receiver type, which the
//     working draft does not require.
//   - spawn_future returns a move-only single-consumer future sender. Its
//     shared state and consumer record honor get_allocator(env); consumer
//     cancellation registers the receiver's concrete stop-token type.
//   - associate/spawn, current-WD token wrap semantics, and async
//     sender-returning simple/counting scope join() are implemented.
//   - on(sender, scheduler, closure) requires the child sender attributes to
//     expose get_completion_scheduler<set_value_t> in this subset.
//   - Execution domains are still a focused draft subset: public
//     transform_sender, connect, and get_completion_signatures(sender, env)
//     share completion-domain-then-start-domain recursive transformation.
//     default_domain honors sender-tag transformation, and apply_sender uses
//     explicit-domain-first then sender-tag fallback dispatch. Legacy
//     two-argument domain transforms remain a source-compatibility extension.
//   - Receiver completion callbacks, including set_value, must be noexcept.
//   - inplace_stop_callback registration currently allocates an internal
//     rendezvous control block, so its constructor is not conditionally
//     noexcept as required by the current working draft.
//   - Non-copyable lvalue senders require explicit std::move(sndr) on
//     standard-shaped paths, matching native handoff expectations.
//   - The older transform_env spelling is not present in the current working
//     draft and is intentionally not exposed.
//
// NOT IMPLEMENTED (Phase 4+):
//   - standard type-erased sender surface.

// Language version guard.
#if __cplusplus < 202002L
#error "Forge <execution> backport requires C++20 or newer"
#endif

#if __has_include(<version>)
#include <version>
#endif

#include <atomic>
#include <concepts>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <mutex>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

#include "execution/detail.hpp"
#include "execution/stop_token.hpp"
#include "execution/env.hpp"
#include "execution/concepts.hpp"
#include "execution/just.hpp"
#include "execution/then.hpp"
#include "execution/sync_wait.hpp"
#include "execution/inline_scheduler.hpp"
#include "execution/run_loop.hpp"
#include "execution/read_env.hpp"
#include "execution/write_env.hpp"
#include "execution/unstoppable.hpp"
#include "execution/upon.hpp"
#include "execution/let.hpp"
#include "execution/stopped_as.hpp"

#include "execution/on.hpp"
#include "execution/into_variant.hpp"
#include "execution/schedule_from.hpp"
#include "execution/continues_on.hpp"
#include "execution/affine.hpp"
#include "execution/bulk.hpp"
#include "execution/split.hpp"
#include "execution/when_all.hpp"
#include "execution/awaitable.hpp"
#include "execution/domain.hpp"
#include "execution/counting_scope.hpp"
#include "execution/associate.hpp"
#include "execution/spawn.hpp"
#include "execution/spawn_future.hpp"
