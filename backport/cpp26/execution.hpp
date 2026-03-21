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
// DEVIATION from the current working draft [exec]:
//   - CPO dispatch uses tag_invoke (P2300 R0-R7 era), not the member-function-
//     first + tag_of_t/domain dispatch adopted in the final standard.
//     tag_invoke is purely an internal implementation mechanism here and is NOT
//     exposed as a user-facing customisation protocol.  When native <execution>
//     support becomes available, Forge disables this backport transparently.
//   - sync_wait uses run_loop per [exec.sync.wait]; receiver env provides
//     get_scheduler -> run_loop::scheduler and get_stop_token -> inplace_stop_token.
//     Static value_type inference uses empty_env for conservative type computation.
//   - NOT IMPLEMENTED: when_all, split, ensure_started, bulk (Phase 2+)
//   - NOT IMPLEMENTED: coroutine/awaitable sender support
//   - NOT IMPLEMENTED: domain-based dispatch
//   - NOT IMPLEMENTED: continues_on (scheduler context transfer after completion)
//   - sender_adaptor_closure<D> CRTP base is available in closures.hpp;
//     existing adaptors (then, upon_*, let_*, stopped_as_*) provide pipe operator|
//     via per-adaptor friend functions.

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
