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

namespace __forge_just {

template<class R, class... Vs>
struct operation : __forge_detail::__immovable {
    using operation_state_concept = operation_state_t;
    [[no_unique_address]] R rcvr_;
    std::tuple<Vs...> values_;

    operation(R rcvr, std::tuple<Vs...> vals)
        : rcvr_(std::move(rcvr)), values_(std::move(vals)) {}

    void start() & noexcept {
        std::apply(
            [&](Vs&&... vs) noexcept {
                std::execution::set_value(std::move(rcvr_), static_cast<Vs&&>(vs)...);
            },
            std::move(values_));
    }
};

template<class... Vs>
struct sender {
    using sender_concept = sender_t;
    std::tuple<Vs...> values_;

    template<class Self, class Env>
    static constexpr auto get_completion_signatures() noexcept
        -> completion_signatures<set_value_t(Vs...)> {
        return {};
    }

    template<receiver R>
    auto connect(R rcvr) && -> operation<R, Vs...> {
        return operation<R, Vs...>(std::move(rcvr), std::move(values_));
    }

    template<receiver R>
        requires (std::copy_constructible<Vs> && ...)
    auto connect(R rcvr) const& -> operation<R, Vs...> {
        return operation<R, Vs...>(std::move(rcvr), values_);
    }

    // Completion is synchronous in start(); there is no associated scheduler.
    auto get_env() const noexcept -> empty_env { return {}; }
};

} // namespace __forge_just

struct just_t {
    template<class... Vs>
        requires (std::move_constructible<std::decay_t<Vs>> && ...)
    [[nodiscard]] auto operator()(Vs&&... vs) const {
        return __forge_just::sender<std::decay_t<Vs>...>{
            std::tuple<std::decay_t<Vs>...>{std::forward<Vs>(vs)...}};
    }
};

inline constexpr just_t just{};

namespace __forge_just_error {

template<class R, class E>
struct operation : __forge_detail::__immovable {
    using operation_state_concept = operation_state_t;
    [[no_unique_address]] R rcvr_;
    E error_;

    operation(R rcvr, E err)
        : rcvr_(std::move(rcvr)), error_(std::move(err)) {}

    void start() & noexcept {
        std::execution::set_error(std::move(rcvr_), std::move(error_));
    }
};

template<class E>
struct sender {
    using sender_concept = sender_t;
    E error_;

    template<class Self, class Env>
    static constexpr auto get_completion_signatures() noexcept
        -> completion_signatures<set_error_t(E)> {
        return {};
    }

    template<receiver R>
    auto connect(R rcvr) && -> operation<R, E> {
        return operation<R, E>(std::move(rcvr), std::move(error_));
    }

    template<receiver R>
        requires std::copy_constructible<E>
    auto connect(R rcvr) const& -> operation<R, E> {
        return operation<R, E>(std::move(rcvr), error_);
    }

    // Completion is synchronous in start(); there is no associated scheduler.
    auto get_env() const noexcept -> empty_env { return {}; }
};

} // namespace __forge_just_error

struct just_error_t {
    template<class E>
        requires std::move_constructible<std::decay_t<E>>
    [[nodiscard]] auto operator()(E&& e) const {
        return __forge_just_error::sender<std::decay_t<E>>{std::forward<E>(e)};
    }
};

inline constexpr just_error_t just_error{};

namespace __forge_just_stopped {

template<class R>
struct operation : __forge_detail::__immovable {
    using operation_state_concept = operation_state_t;
    [[no_unique_address]] R rcvr_;

    explicit operation(R rcvr) : rcvr_(std::move(rcvr)) {}

    void start() & noexcept { std::execution::set_stopped(std::move(rcvr_)); }
};

struct sender {
    using sender_concept = sender_t;

    template<class Self, class Env>
    static constexpr auto get_completion_signatures() noexcept
        -> completion_signatures<set_stopped_t()> {
        return {};
    }

    template<receiver R>
    auto connect(R rcvr) && -> operation<R> {
        return operation<R>(std::move(rcvr));
    }

    template<receiver R>
    auto connect(R rcvr) const& -> operation<R> {
        return operation<R>(std::move(rcvr));
    }

    // Completion is synchronous in start(); there is no associated scheduler.
    auto get_env() const noexcept -> empty_env { return {}; }
};

} // namespace __forge_just_stopped

struct just_stopped_t {
    [[nodiscard]] auto operator()() const noexcept {
        return __forge_just_stopped::sender{};
    }
};

inline constexpr just_stopped_t just_stopped{};

} // namespace std::execution
