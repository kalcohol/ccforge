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

#include "concepts.hpp"

namespace std::execution {

namespace __forge_just {

template<class R, class... Vs>
struct operation : __forge_detail::__immovable {
    using operation_state_concept = operation_state_t;
    [[no_unique_address]] R rcvr_;
    std::tuple<Vs...> values_;

    operation(R rcvr, std::tuple<Vs...> vals)
        : rcvr_(std::move(rcvr)), values_(std::move(vals)) {}

    friend void tag_invoke(start_t, operation& self) noexcept {
        std::apply(
            [&](Vs&&... vs) noexcept {
                std::execution::set_value(std::move(self.rcvr_), static_cast<Vs&&>(vs)...);
            },
            std::move(self.values_));
    }
};

template<class... Vs>
struct sender {
    using sender_concept = sender_t;
    std::tuple<Vs...> values_;

    friend auto tag_invoke(get_completion_signatures_t, const sender&, auto) noexcept
        -> completion_signatures<set_value_t(Vs...)> {
        return {};
    }

    template<receiver R>
    friend auto tag_invoke(connect_t, sender self, R rcvr) -> operation<R, Vs...> {
        return operation<R, Vs...>(std::move(rcvr), std::move(self.values_));
    }

    // TODO([exec.just]): expose completion scheduler in env via just-env type
    friend auto tag_invoke(get_env_t, const sender&) noexcept -> empty_env { return {}; }
};

} // namespace __forge_just

template<class... Vs>
    requires (std::move_constructible<std::decay_t<Vs>> && ...)
[[nodiscard]] auto just(Vs&&... vs) {
    return __forge_just::sender<std::decay_t<Vs>...>{
        std::tuple<std::decay_t<Vs>...>{std::forward<Vs>(vs)...}};
}

namespace __forge_just_error {

template<class R, class E>
struct operation : __forge_detail::__immovable {
    using operation_state_concept = operation_state_t;
    [[no_unique_address]] R rcvr_;
    E error_;

    operation(R rcvr, E err)
        : rcvr_(std::move(rcvr)), error_(std::move(err)) {}

    friend void tag_invoke(start_t, operation& self) noexcept {
        std::execution::set_error(std::move(self.rcvr_), std::move(self.error_));
    }
};

template<class E>
struct sender {
    using sender_concept = sender_t;
    E error_;

    friend auto tag_invoke(get_completion_signatures_t, const sender&, auto) noexcept
        -> completion_signatures<set_error_t(E)> {
        return {};
    }

    template<receiver R>
    friend auto tag_invoke(connect_t, sender self, R rcvr) -> operation<R, E> {
        return operation<R, E>(std::move(rcvr), std::move(self.error_));
    }

    // TODO([exec.just]): expose completion scheduler in env via just-env type
    friend auto tag_invoke(get_env_t, const sender&) noexcept -> empty_env { return {}; }
};

} // namespace __forge_just_error

template<class E>
    requires std::move_constructible<std::decay_t<E>>
[[nodiscard]] auto just_error(E&& e) {
    return __forge_just_error::sender<std::decay_t<E>>{std::forward<E>(e)};
}

namespace __forge_just_stopped {

template<class R>
struct operation : __forge_detail::__immovable {
    using operation_state_concept = operation_state_t;
    [[no_unique_address]] R rcvr_;

    explicit operation(R rcvr) : rcvr_(std::move(rcvr)) {}

    friend void tag_invoke(start_t, operation& self) noexcept { std::execution::set_stopped(std::move(self.rcvr_)); }
};

struct sender {
    using sender_concept = sender_t;

    friend auto tag_invoke(get_completion_signatures_t, const sender&, auto) noexcept
        -> completion_signatures<set_stopped_t()> {
        return {};
    }

    template<receiver R>
    friend auto tag_invoke(connect_t, sender, R rcvr) -> operation<R> {
        return operation<R>(std::move(rcvr));
    }

    // TODO([exec.just]): expose completion scheduler in env via just-env type
    friend auto tag_invoke(get_env_t, const sender&) noexcept -> empty_env { return {}; }
};

} // namespace __forge_just_stopped

[[nodiscard]] inline auto just_stopped() noexcept { return __forge_just_stopped::sender{}; }

} // namespace std::execution
