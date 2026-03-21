// MIT License
//
// Copyright (c) 2026 Forge Project
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

// NOTE: This is a Forge backport of a small P2300 sender/receiver MVP.
// It is intentionally minimal and correctness-first.
//
// IMPLEMENTATION STATUS (Forge C++26 execution backport — Phase 1+2+3 complete):
//
// IMPLEMENTED:
//   Sender factories : just, just_error, just_stopped, read_env
//   Value adaptors   : then, upon_error, upon_stopped
//   Sender adaptors  : let_value, let_error, let_stopped
//   Scheduler ops    : starts_on, continues_on (schedule_from), bulk (serial)
//   Combinators      : into_variant, when_all, split, ensure_started, start_detached
//   Consumers        : sync_wait, sync_wait_with_variant (via this_thread)
//   Stopped utils    : stopped_as_optional, stopped_as_error
//   Schedulers       : inline_scheduler, run_loop (mutex+cv)
//   Stop tokens      : inplace_stop_source/token/callback, never_stop_token,
//                      any_stop_token, stoppable_token concepts
//   Coroutine bridge : as_awaitable, with_awaitable_senders (C++20 coroutines)
//   Infra            : sender_adaptor_closure CRTP, transform_completion_signatures,
//                      enable_sender, get_completion_scheduler CPO,
//                      SBO+heap storage abstraction, CPO member-function-first dispatch
//
// DEVIATIONS from current working draft [exec]:
//   - CPO dispatch: tag_invoke internally (not final member-function-first mechanism);
//     not user-visible; new Phase 3+ types use member-function-first dispatch.
//   - sync_wait value_type inference uses empty_env for conservative type computation.
//   - ensure_started delegates to split (does not eagerly start on detached thread).
//   - Domain-based dispatch not implemented (always uses default_domain).
//
// NOT IMPLEMENTED (Phase 4+):
//   - when_all, split: TSAN unverifiable on this host (kernel mmap_rnd_bits issue)
//     but logic tested with concurrent GTest scenarios passing on GCC/Zig.
//   - async_scope, counting_scope
//   - type-erased sender (sender erasure patterns)

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
#include "execution/detail/storage.hpp"
#include "execution/stop_token.hpp"
#include "execution/env.hpp"
#include "execution/concepts.hpp"
#include "execution/just.hpp"
#include "execution/then.hpp"
#include "execution/sync_wait.hpp"
#include "execution/inline_scheduler.hpp"
#include "execution/run_loop.hpp"
#include "execution/read_env.hpp"
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
