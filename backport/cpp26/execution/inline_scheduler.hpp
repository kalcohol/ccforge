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

namespace std::execution {

class inline_scheduler;

namespace __forge_inline {

struct __attrs {
    constexpr auto query(get_forward_progress_guarantee_t) const noexcept
        -> forward_progress_guarantee {
        return forward_progress_guarantee::weakly_parallel;
    }

    template<class Env>
        requires requires(Env&& e) { std::execution::get_scheduler(static_cast<Env&&>(e)); }
    constexpr decltype(auto) query(get_completion_scheduler_t<set_value_t>, Env&& e) const
        noexcept(noexcept(std::execution::get_scheduler(static_cast<Env&&>(e)))) {
        return std::execution::get_scheduler(static_cast<Env&&>(e));
    }

    template<class Env>
    constexpr auto query(get_completion_domain_t<set_value_t>, Env&& e) const
        noexcept(noexcept(std::execution::get_domain(static_cast<Env&&>(e))))
        -> decltype(std::execution::get_domain(static_cast<Env&&>(e))) {
        return std::execution::get_domain(static_cast<Env&&>(e));
    }
};

struct env : __attrs {
    using __attrs::query;
    template<class Env>
        requires (!requires(Env&& e) { std::execution::get_scheduler(static_cast<Env&&>(e)); })
    constexpr auto query(get_completion_scheduler_t<set_value_t>, Env&&) const noexcept
        -> inline_scheduler;
};

template<class R>
struct operation : __forge_detail::__immovable {
    using operation_state_concept = operation_state_t;
    [[no_unique_address]] R rcvr_;

    explicit operation(R rcvr) noexcept(std::is_nothrow_move_constructible_v<R>)
        : rcvr_(std::move(rcvr)) {}

    void start() & noexcept { std::execution::set_value(std::move(rcvr_)); }
};

struct sender {
    using sender_concept = sender_t;

    template<class Self, class Env>
    static constexpr auto get_completion_signatures() noexcept
        -> completion_signatures<set_value_t()> {
        return {};
    }

    template<receiver R>
    auto connect(R rcvr) && noexcept(std::is_nothrow_move_constructible_v<R>) -> operation<R> {
        return operation<R>(std::move(rcvr));
    }

    template<receiver R>
    auto connect(R rcvr) const& noexcept(std::is_nothrow_move_constructible_v<R>) -> operation<R> {
        return operation<R>(std::move(rcvr));
    }

    constexpr auto get_env() const noexcept -> env { return {}; }
};

} // namespace __forge_inline

class inline_scheduler {
public:
    using scheduler_concept = scheduler_t;

    inline_scheduler() noexcept = default;

    [[nodiscard]] constexpr __forge_inline::sender schedule() const noexcept { return {}; }

    template<class Query, class... Args>
        requires requires(const __forge_inline::__attrs& attrs, Query q, Args&&... args) {
            attrs.query(q, static_cast<Args&&>(args)...);
        }
    constexpr decltype(auto) query(Query q, Args&&... args) const
        noexcept(noexcept(__forge_inline::__attrs{}.query(q, static_cast<Args&&>(args)...))) {
        return __forge_inline::__attrs{}.query(q, static_cast<Args&&>(args)...);
    }

    constexpr bool operator==(const inline_scheduler&) const noexcept = default;

};

namespace __forge_inline {
template<class Env>
    requires (!requires(Env&& e) { std::execution::get_scheduler(static_cast<Env&&>(e)); })
constexpr auto env::query(get_completion_scheduler_t<set_value_t>, Env&&) const noexcept
    -> inline_scheduler { return {}; }
} // namespace __forge_inline

} // namespace std::execution
