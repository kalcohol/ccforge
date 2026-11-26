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

#define FORGE_EXECUTION_BACKPORT_ACTIVE 1

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
//   Forge extensions : forge::transfer_just, forge::bulk*
//   Combinators      : into_variant, when_all, when_all_with_variant,
//                      associate, spawn, spawn_future
//   Consumers        : sync_wait, sync_wait_with_variant (via this_thread)
//   Stopped utils    : stopped_as_optional, stopped_as_error, unstoppable
//   Schedulers       : inline_scheduler, run_loop (mutex+cv), task_scheduler
//   Scheduler SPI    : parallel scheduler replacement proxy and backend types
//                      (no parallel scheduler frontend or replacement query)
//   Stop tokens      : inplace_stop_source/token/callback, never_stop_token,
//                      stoppable_token concepts
//   Coroutine bridge : as_awaitable, with_awaitable_senders (C++20 coroutines)
//   Infra            : completion_signatures_of_t,
//                      value_types_of_t, error_types_of_t, sends_stopped,
//                      private per-adaptor signature transforms, enable_sender,
//                      get_start_scheduler/get_delegation_scheduler,
//                      forwarding_query, get_forward_progress_guarantee,
//                      get_allocator/get_scheduler_allocator CPOs,
//                      get_completion_scheduler/get_completion_domain CPOs,
//                      get_await_completion_adaptor CPO,
//                      transform_sender/apply_sender domain dispatch,
//                      Member-based CPO customization
//   Verification     : execution subset covered by dedicated ThreadSanitizer
//                      and ASan+UBSan container configurations.
//
//   - sync_wait requires exactly one value completion signature in its
//     run_loop environment. Use sync_wait_with_variant for multiple signatures.
//     For a sender with no value signature, connect and start with a receiver.
//   - when_all accepts at most one value completion signature per child.
//     Use when_all_with_variant for children with multiple signatures.
//   - The bulk algorithms run serially as permitted by [exec.bulk]/9.
//     See "bulk" and "bulk_chunked, bulk_unchunked" in the ledger.
//   - task_scheduler implements ordinary scheduling only. Its domain uses
//     the serial bulk fallback until stage 2. No task feature macro is defined.
//
// DEVIATIONS from current working draft [exec]:
// Entries below refer to docs/roadmap/execution-conformance-ledger.md.
//   - Domain customization has limited sender decomposition. Member connect
//     does not enforce all Mandates. See "sender_tag, receiver_tag,
//     operation_state_tag, scheduler_tag, tag_of_t" and "Core CPO customization".
//   - Completion-signature queries use a value-taking CPO with substitution
//     failure in place of consteval diagnostics. See "completion_signatures,
//     completion_signatures_of_t, value/error utilities". Senders can also
//     customize signatures through an instance member taking an environment.
//     See the instance-member entry under actual query classification.
//   - prop defers its callable Mandate to avoid recursive scheduler checks.
//     See "env, prop".
//   - Forwarding views query the underlying environment as a const lvalue.
//     State-dependent forwarding queries use a value-initialized stand-in
//     when available in constant evaluation and are otherwise filtered out.
//     See the "forwarding_query" row and the D3 forwarding-view operand entry.
//     Some static adaptors use different environment value categories for
//     signatures and connect. The counting_scope stop wrapper takes the query
//     tag by value and forwards it as a non-const lvalue. See the D3 static
//     adaptor and counting_scope entries under actual query classification.
//   - stopped_as_optional defers its creation Mandate to the actual environment.
//     See "stopped_as_optional, stopped_as_error".
//   - GET-AWAITER is emulated, the awaitable connect operation is movable, and
//     the sender bridge defers connect and uses symmetric transfer. A prepared
//     sender with multiple value signatures falls back to the original
//     expression. See the coroutine bridge entries.
//   - spawn_future allocates a consumer record at connect. See "Other Residuals".
//   - sync_wait and sync_wait_with_variant retain a completion-domain fallback.
//     See the wait consumer entries.
//   - sync_wait_with_variant returns the reference implementation's flat
//     optional as a drafting-error exception. See the [exec.sync.wait.var] entry.
//
// Missing standard facilities are listed in the ledger's coverage matrix.

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
#include "execution/parallel_scheduler_replacement.hpp"
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
#include "execution/task_scheduler.hpp"
#include "execution/bulk.hpp"
#include "execution/when_all.hpp"
#include "execution/awaitable.hpp"
#include "execution/counting_scope.hpp"
#include "execution/associate.hpp"
#include "execution/spawn.hpp"
#include "execution/spawn_future.hpp"
#include "execution/sender_adaptor_closure.hpp"

namespace std::execution {
using spawn_t = __forge_spawn::__spawn_t;
using associate_t = __forge_associate::__associate_t;
} // namespace std::execution
