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
// IMPLEMENTATION STATUS (Forge C++26 execution backport — Phase 1-4 subset):
//
// IMPLEMENTED:
//   Sender factories : just, just_error, just_stopped, read_env
//   Value adaptors   : then, upon_error, upon_stopped
//   Sender adaptors  : let_value, let_error, let_stopped, write_env
//   Scheduler ops    : starts_on, continues_on (schedule_from), transfer_just,
//                      bulk (serial)
//   Combinators      : into_variant, when_all, when_all_with_variant, split,
//                      ensure_started, start_detached, spawn_future
//   Consumers        : sync_wait, sync_wait_with_variant (via this_thread)
//   Stopped utils    : stopped_as_optional, stopped_as_error
//   Schedulers       : inline_scheduler, run_loop (mutex+cv)
//   Stop tokens      : inplace_stop_source/token/callback, never_stop_token,
//                      any_stop_token, stoppable_token concepts
//   Coroutine bridge : as_awaitable, with_awaitable_senders (C++20 coroutines)
//   Infra            : completion_signatures_of_t,
//                      transform_completion_signatures, enable_sender,
//                      get_completion_scheduler/get_completion_domain CPOs,
//                      CPO member-function-first dispatch
//   Verification     : execution subset covered by dedicated ThreadSanitizer
//                      and ASan+UBSan container configurations.
//
// DEVIATIONS from current working draft [exec]:
//   - CPO customization is still tag_invoke-based internally for Forge-provided
//     senders/receivers; the C++26 draft primarily specifies member customization.
//   - sync_wait value_type inference uses empty_env for conservative type computation.
//   - as_awaitable preserves Forge's historical tuple result for a single
//     value-completion shape; multiple value alternatives produce
//     variant<tuple<...>, ...>.
//   - ensure_started eagerly starts work and caches for multiple consumers.
//     Cached values are delivered as lvalues, so move-only value results are
//     not supported; abandoning the returned sender does not request stop.
//   - start_detached terminates on set_error; attach an error-handling adaptor
//     before detaching if failures are expected.
//   - spawn_future returns a move-only single-consumer future sender. Its
//     shared state honors get_allocator(env), but auxiliary consumer/callback
//     allocations are not fully allocator-aware.
//   - counting_scope is stop-aware, but join() is still Forge's blocking
//     compatibility extension rather than the standard sender-returning shape.
//   - Execution domains are still a focused draft subset: connect uses the
//     receiver-env start domain and supports transform_sender recovery plus
//     transform_env wrapping, but not the full recursive transform_sender model.
//   - Receiver completion callbacks, including set_value, must be noexcept.
//   - Non-copyable lvalue senders must be connected as rvalues with std::move.
//
// NOT IMPLEMENTED (Phase 4+):
//   - sender-returning async-scope join.
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
#include "execution/upon.hpp"
#include "execution/let.hpp"
#include "execution/stopped_as.hpp"

#include "execution/on.hpp"
#include "execution/into_variant.hpp"
#include "execution/continues_on.hpp"
#include "execution/bulk.hpp"
#include "execution/start_detached.hpp"
#include "execution/split.hpp"
#include "execution/when_all.hpp"
#include "execution/ensure_started.hpp"
#include "execution/any_stop_token.hpp"
#include "execution/awaitable.hpp"
#include "execution/domain.hpp"
#include "execution/counting_scope.hpp"
#include "execution/spawn_future.hpp"
