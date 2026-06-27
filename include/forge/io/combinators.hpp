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

#include <forge/io/coro.hpp>
#include <forge/io/result.hpp>

#include <concepts>
#include <cstddef>
#include <exception>
#include <execution>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <system_error>
#include <type_traits>
#include <utility>

namespace forge::io {

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L

template<class First, class Second>
struct when_all_result {
    std::optional<First> first;
    std::optional<Second> second;
};

namespace __io_combinator_detail {

template<class T>
struct is_io_result : std::false_type {};

template<class... Ts>
struct is_io_result<io_result<Ts...>> : std::true_type {};

template<class T>
inline constexpr bool is_io_result_v = is_io_result<T>::value;

template<class First, class Second, class Receiver>
class shared_state {
public:
    using payload_t = when_all_result<First, Second>;
    using aggregate_t = io_result<payload_t>;

    explicit shared_state(Receiver receiver)
        : receiver_(std::move(receiver))
    {}

    [[nodiscard]] auto stop_token() noexcept -> std::inplace_stop_token {
        return stop_source_.get_token();
    }

    [[nodiscard]] auto stop_source() noexcept -> std::inplace_stop_source& {
        return stop_source_;
    }

    [[nodiscard]] auto receiver() noexcept -> Receiver& {
        return *receiver_;
    }

    template<std::size_t I, class Result>
    auto set_value(Result result) noexcept -> void {
        const bool should_stop = result.status() != io_status::value;
        auto action = store_result<I>(std::move(result));
        if (should_stop) {
            stop_source_.request_stop();
        }
        deliver(std::move(action));
    }

    template<std::size_t I>
    auto set_stopped() noexcept -> void {
        auto action = store_stopped<I>();
        stop_source_.request_stop();
        deliver(std::move(action));
    }

    template<class Error>
    auto set_start_error(Error&& error) noexcept -> void {
        std::exception_ptr ep;
        if constexpr (std::is_same_v<std::decay_t<Error>, std::exception_ptr>) {
            ep = static_cast<Error&&>(error);
        } else {
            ep = std::make_exception_ptr(static_cast<Error&&>(error));
        }
        auto action = store_start_error(std::move(ep));
        stop_source_.request_stop();
        deliver(std::move(action));
    }

    template<std::size_t I, class Error>
    auto set_error(Error&& error) noexcept -> void {
        std::exception_ptr ep;
        if constexpr (std::is_same_v<std::decay_t<Error>, std::exception_ptr>) {
            ep = static_cast<Error&&>(error);
        } else {
            ep = std::make_exception_ptr(static_cast<Error&&>(error));
        }
        auto action = store_unexpected_error<I>(std::move(ep));
        stop_source_.request_stop();
        deliver(std::move(action));
    }

private:
    enum class action_kind {
        none,
        value,
        error,
        stopped
    };

    struct completion_action {
        action_kind kind = action_kind::none;
        std::optional<Receiver> receiver;
        std::optional<aggregate_t> value;
        std::exception_ptr error;
    };

    template<std::size_t I, class Result>
    auto store_result(Result result) noexcept -> completion_action {
        std::lock_guard lock{mtx_};
        if constexpr (I == 0) {
            if (first_done_) {
                return {};
            }
            first_ = std::move(result);
            first_done_ = true;
        } else {
            if (second_done_) {
                return {};
            }
            second_ = std::move(result);
            second_done_ = true;
        }
        --pending_;
        return maybe_complete_locked();
    }

    template<std::size_t I>
    auto store_stopped() noexcept -> completion_action {
        std::lock_guard lock{mtx_};
        if constexpr (I == 0) {
            if (first_done_) {
                return {};
            }
            first_stopped_ = true;
            first_done_ = true;
        } else {
            if (second_done_) {
                return {};
            }
            second_stopped_ = true;
            second_done_ = true;
        }
        --pending_;
        return maybe_complete_locked();
    }

    auto store_start_error(std::exception_ptr ep) noexcept
        -> completion_action {
        std::lock_guard lock{mtx_};
        if (completion_sent_) {
            return {};
        }
        unexpected_error_ = std::move(ep);
        pending_ = 0;
        first_done_ = true;
        second_done_ = true;
        return maybe_complete_locked();
    }

    template<std::size_t I>
    auto store_unexpected_error(std::exception_ptr ep) noexcept
        -> completion_action {
        std::lock_guard lock{mtx_};
        if constexpr (I == 0) {
            if (first_done_) {
                return {};
            }
            first_done_ = true;
        } else {
            if (second_done_) {
                return {};
            }
            second_done_ = true;
        }
        if (!unexpected_error_) {
            unexpected_error_ = std::move(ep);
        }
        --pending_;
        return maybe_complete_locked();
    }

    auto maybe_complete_locked() -> completion_action {
        if (completion_sent_ || pending_ != 0) {
            return {};
        }
        completion_sent_ = true;

        completion_action action;
        action.receiver = std::move(receiver_);

        if (unexpected_error_) {
            action.kind = action_kind::error;
            action.error = std::move(unexpected_error_);
            return action;
        }

        payload_t payload;
        payload.first = std::move(first_);
        payload.second = std::move(second_);

        if (payload.first && payload.first->status() == io_status::error) {
            auto error = payload.first->error();
            action.kind = action_kind::value;
            action.value = aggregate_t::failure(error, std::move(payload));
            return action;
        }
        if (payload.second && payload.second->status() == io_status::error) {
            auto error = payload.second->error();
            action.kind = action_kind::value;
            action.value = aggregate_t::failure(error, std::move(payload));
            return action;
        }
        if ((payload.first && payload.first->eof()) ||
            (payload.second && payload.second->eof())) {
            action.kind = action_kind::value;
            action.value = aggregate_t::end_of_file(std::move(payload));
            return action;
        }
        if (first_stopped_ || second_stopped_ ||
            !payload.first || !payload.second) {
            action.kind = action_kind::stopped;
            return action;
        }

        action.kind = action_kind::value;
        action.value = aggregate_t::success(std::move(payload));
        return action;
    }

    static auto deliver(completion_action action) noexcept -> void {
        if (!action.receiver) {
            return;
        }

        switch (action.kind) {
        case action_kind::none:
            break;
        case action_kind::value:
            std::execution::set_value(
                std::move(*action.receiver),
                std::move(*action.value));
            break;
        case action_kind::error:
            std::execution::set_error(
                std::move(*action.receiver),
                std::move(action.error));
            break;
        case action_kind::stopped:
            std::execution::set_stopped(std::move(*action.receiver));
            break;
        }
    }

    std::mutex mtx_;
    std::optional<Receiver> receiver_;
    std::inplace_stop_source stop_source_{};
    std::size_t pending_ = 2;
    bool completion_sent_ = false;
    bool first_done_ = false;
    bool second_done_ = false;
    bool first_stopped_ = false;
    bool second_stopped_ = false;
    std::optional<First> first_;
    std::optional<Second> second_;
    std::exception_ptr unexpected_error_;
};

template<std::size_t I, class State, class Result>
struct child_receiver {
    using receiver_concept = std::execution::receiver_t;

    struct env {
        std::shared_ptr<State> state;

        friend auto tag_invoke(
            std::execution::get_stop_token_t,
            const env& self) noexcept -> std::inplace_stop_token {
            return self.state->stop_token();
        }
    };

    std::shared_ptr<State> state;

    auto set_value(Result result) && noexcept -> void {
        state->template set_value<I>(std::move(result));
    }

    template<class Error>
    auto set_error(Error&& error) && noexcept -> void {
        state->template set_error<I>(static_cast<Error&&>(error));
    }

    auto set_stopped() && noexcept -> void {
        state->template set_stopped<I>();
    }

    [[nodiscard]] auto get_env() const noexcept -> env {
        return env{state};
    }
};

template<class First, class Second>
class when_all_sender {
public:
    using sender_concept = std::execution::sender_t;
    using payload_t = when_all_result<First, Second>;
    using aggregate_t = io_result<payload_t>;

    when_all_sender(io_task<First> first, io_task<Second> second, io_env env)
        : first_(std::move(first))
        , second_(std::move(second))
        , env_(std::move(env))
    {}

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<
            std::execution::set_value_t(aggregate_t),
            std::execution::set_error_t(std::exception_ptr),
            std::execution::set_stopped_t()> {
        return {};
    }

    [[nodiscard]] auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }

    template<std::execution::receiver Receiver>
    class operation {
    public:
        using state_t = shared_state<First, Second, Receiver>;
        using first_receiver_t = child_receiver<0, state_t, First>;
        using second_receiver_t = child_receiver<1, state_t, Second>;
        using receiver_env_t = std::execution::env_of_t<Receiver>;
        using receiver_stop_token_t = decltype(
            std::execution::get_stop_token(std::declval<receiver_env_t>()));
        using first_sender_t = decltype(as_sender(
            std::declval<io_task<First>>(),
            std::declval<io_env>()));
        using second_sender_t = decltype(as_sender(
            std::declval<io_task<Second>>(),
            std::declval<io_env>()));
        using first_op_t = std::execution::connect_result_t<
            first_sender_t,
            first_receiver_t>;
        using second_op_t = std::execution::connect_result_t<
            second_sender_t,
            second_receiver_t>;

        operation(
            io_task<First> first,
            io_task<Second> second,
            io_env env,
            Receiver receiver)
            : state_(std::make_shared<state_t>(std::move(receiver)))
            , first_op_(std::execution::connect(
                  as_sender(std::move(first), env),
                  first_receiver_t{state_}))
            , second_op_(std::execution::connect(
                  as_sender(std::move(second), std::move(env)),
                  second_receiver_t{state_}))
        {}

        operation(operation&&) = delete;
        auto operator=(operation&&) -> operation& = delete;
        operation(const operation&) = delete;
        auto operator=(const operation&) -> operation& = delete;

        auto start() & noexcept -> void {
            try {
                receiver_stop_.install(
                    std::execution::get_stop_token(
                        std::execution::get_env(state_->receiver())),
                    state_->stop_source());
                std::execution::start(first_op_);
                std::execution::start(second_op_);
            } catch (...) {
                state_->set_start_error(std::current_exception());
            }
        }

    private:
        std::shared_ptr<state_t> state_;
        first_op_t first_op_;
        second_op_t second_op_;
        __coro_detail::optional_stop_callback<receiver_stop_token_t>
            receiver_stop_;
    };

    template<std::execution::receiver Receiver>
    [[nodiscard]] auto connect(Receiver receiver) && -> operation<Receiver> {
        return operation<Receiver>{
            std::move(first_),
            std::move(second_),
            std::move(env_),
            std::move(receiver)};
    }

private:
    io_task<First> first_;
    io_task<Second> second_;
    io_env env_;
};

} // namespace __io_combinator_detail

template<class First, class Second>
    requires __io_combinator_detail::is_io_result_v<First> &&
             __io_combinator_detail::is_io_result_v<Second>
[[nodiscard]] auto when_all_results(
    io_task<First> first,
    io_task<Second> second,
    io_env env = {}) {
    return __io_combinator_detail::when_all_sender<First, Second>{
        std::move(first),
        std::move(second),
        std::move(env)};
}

#endif // __cpp_impl_coroutine

} // namespace forge::io
