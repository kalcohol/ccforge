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

#include <execution>
#include <utility>

namespace forge {

// The historical empty environment keeps its type identity in the backport.
#if defined(FORGE_EXECUTION_BACKPORT_ACTIVE)
using empty_env = std::execution::__empty_env;
#else
using empty_env = std::execution::env<>;
#endif

template<std::execution::scheduler Scheduler, class... Vs>
    requires (std::move_constructible<std::decay_t<Vs>> && ...)
[[nodiscard]] auto transfer_just(Scheduler sch, Vs&&... vs) {
    return std::execution::continues_on(
        std::execution::just(std::forward<Vs>(vs)...), std::move(sch));
}

#if defined(FORGE_EXECUTION_BACKPORT_ACTIVE)
// Diagnostic snapshots are available only for the backport scope types.
template<class Scope>
    requires (std::same_as<Scope, std::execution::simple_counting_scope> ||
              std::same_as<Scope, std::execution::counting_scope>)
[[nodiscard]] std::size_t scope_count(const Scope& scope) noexcept {
    return scope.__count();
}
template<class Scope>
    requires (std::same_as<Scope, std::execution::simple_counting_scope> ||
              std::same_as<Scope, std::execution::counting_scope>)
[[nodiscard]] bool scope_is_closed(const Scope& scope) noexcept {
    return scope.__is_closed();
}

// split and the legacy tuple-or-variant wait rely on backport implementation types.
template<std::execution::sender S>
[[nodiscard]] auto split(S&& sndr) {
    return std::execution::__split(std::forward<S>(sndr));
}
inline constexpr std::this_thread::__sync_wait_impl_t sync_wait{};
#else
// Native implementations provide the standard single-value wait contract.
inline constexpr auto& sync_wait = std::this_thread::sync_wait;
#endif

} // namespace forge
