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

#include "concepts.hpp"
#include "continues_on.hpp"
#include "env.hpp"
#include "unstoppable.hpp"

#include <type_traits>
#include <utility>

namespace std::execution {

namespace __forge_affine {

template<class Scheduler>
struct __unstoppable_scheduler {
    using scheduler_concept = scheduler_t;

    Scheduler __scheduler;

    [[nodiscard]] auto schedule()
        noexcept(noexcept(std::execution::unstoppable(
            std::execution::schedule(__scheduler)))) {
        return std::execution::unstoppable(
            std::execution::schedule(__scheduler));
    }

    friend bool operator==(
        const __unstoppable_scheduler&,
        const __unstoppable_scheduler&) noexcept = default;
};

template<class S>
struct __sender {
    using sender_concept = sender_t;
    using source_t = S;

    S __source;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept {
        using self_t = std::remove_cvref_t<Self>;
        using scheduler_t = std::remove_cvref_t<decltype(
            std::execution::get_start_scheduler(std::declval<Env>()))>;
        using shifted_t = decltype(std::execution::continues_on(
            std::declval<typename self_t::source_t>(),
            std::declval<__unstoppable_scheduler<scheduler_t>>()));
        return decltype(std::execution::get_completion_signatures(
            std::declval<shifted_t>(), std::declval<Env>())){};
    }

    template<receiver R>
    auto connect(R receiver) && {
        auto scheduler = std::execution::get_start_scheduler(
            std::execution::get_env(receiver));
        using scheduler_t = std::remove_cvref_t<decltype(scheduler)>;
        auto shifted = std::execution::continues_on(
            std::move(__source),
            __unstoppable_scheduler<scheduler_t>{std::move(scheduler)});
        return std::execution::connect(std::move(shifted), std::move(receiver));
    }

    template<receiver R>
        requires std::copy_constructible<S>
    auto connect(R receiver) const& {
        auto scheduler = std::execution::get_start_scheduler(
            std::execution::get_env(receiver));
        using scheduler_t = std::remove_cvref_t<decltype(scheduler)>;
        auto shifted = std::execution::continues_on(
            __source,
            __unstoppable_scheduler<scheduler_t>{std::move(scheduler)});
        return std::execution::connect(std::move(shifted), std::move(receiver));
    }

    auto get_env() const noexcept -> empty_env {
        return {};
    }
};

} // namespace __forge_affine

struct affine_t {
    template<sender S>
    [[nodiscard]] auto operator()(S&& sndr) const {
        return __forge_affine::__sender<std::decay_t<S>>{
            __forge_detail::__forward_as_given(std::forward<S>(sndr))};
    }

    template<sender S>
    friend auto operator|(S&& sndr, affine_t self) {
        return self(std::forward<S>(sndr));
    }
};

inline constexpr affine_t affine{};

} // namespace std::execution
