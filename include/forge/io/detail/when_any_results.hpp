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

#include <forge/io/detail/combinator_common.hpp>

#include <exception>
#include <execution>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <utility>

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L

namespace forge::io {

template<class First, class Second>
struct when_any_result;

namespace __io_combinator_detail {

template<class First, class Second, class Receiver>
class when_any_shared_state {
    enum class action_kind {
        none,
        value,
        error,
        stopped
    };

    enum class terminal_kind {
        none,
        result,
        error,
        stopped
    };

    struct completion_action {
        action_kind kind = action_kind::none;
        Receiver* receiver = nullptr;
        std::optional<io_result<when_any_result<First, Second>>> value;
        std::exception_ptr error;
    };

    struct store_outcome {
        bool duplicate = false;
        bool winner = false;
    };

public:
    using payload_t = when_any_result<First, Second>;
    using aggregate_t = io_result<payload_t>;

    explicit when_any_shared_state(Receiver receiver)
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
    auto set_value(Result&& result) noexcept -> void {
        const auto outcome =
            store_result<I>(static_cast<Result&&>(result));
        if (outcome.duplicate) {
            return;
        }
        if (outcome.winner) {
            stop_source_.request_stop();
        }
        completion_action action;
        finish_child(action);
        deliver(action);
    }

    template<std::size_t I>
    auto set_stopped() noexcept -> void {
        const auto outcome = store_stopped<I>();
        if (outcome.duplicate) {
            return;
        }
        if (outcome.winner) {
            stop_source_.request_stop();
        }
        completion_action action;
        finish_child(action);
        deliver(action);
    }

    template<class Error>
    auto set_start_error(Error&& error) noexcept -> void {
        std::exception_ptr ep;
        if constexpr (std::is_same_v<std::decay_t<Error>, std::exception_ptr>) {
            ep = static_cast<Error&&>(error);
        } else {
            ep = std::make_exception_ptr(static_cast<Error&&>(error));
        }
        completion_action action;
        store_start_error(std::move(ep), action);
        stop_source_.request_stop();
        deliver(action);
    }

    template<std::size_t I, class Error>
    auto set_error(Error&& error) noexcept -> void {
        std::exception_ptr ep;
        if constexpr (std::is_same_v<std::decay_t<Error>, std::exception_ptr>) {
            ep = static_cast<Error&&>(error);
        } else {
            ep = std::make_exception_ptr(static_cast<Error&&>(error));
        }
        const auto outcome = store_error<I>(std::move(ep));
        if (outcome.duplicate) {
            return;
        }
        if (outcome.winner) {
            stop_source_.request_stop();
        }
        completion_action action;
        finish_child(action);
        deliver(action);
    }

private:
    template<std::size_t I, class Result>
    auto store_result(Result&& result) noexcept -> store_outcome {
        std::lock_guard lock{mtx_};
        if (is_done_locked<I>()) {
            return {.duplicate = true};
        }
        mark_done_locked<I>();
        const bool winner = select_winner_locked<I>(terminal_kind::result);
        try {
            result_slot_locked<I>().emplace(static_cast<Result&&>(result));
        } catch (...) {
            if (winner) {
                winner_kind_ = terminal_kind::error;
                winner_error_ = std::current_exception();
            }
        }
        return {.winner = winner};
    }

    template<std::size_t I>
    auto store_stopped() noexcept -> store_outcome {
        std::lock_guard lock{mtx_};
        if (is_done_locked<I>()) {
            return {.duplicate = true};
        }
        mark_done_locked<I>();
        return {
            .winner = select_winner_locked<I>(terminal_kind::stopped)};
    }

    template<std::size_t I>
    auto store_error(std::exception_ptr error) noexcept -> store_outcome {
        std::lock_guard lock{mtx_};
        if (is_done_locked<I>()) {
            return {.duplicate = true};
        }
        mark_done_locked<I>();
        const bool winner = select_winner_locked<I>(terminal_kind::error);
        if (winner) {
            winner_error_ = std::move(error);
        }
        return {.winner = winner};
    }

    auto store_start_error(
        std::exception_ptr error,
        completion_action& action) noexcept -> void {
        std::lock_guard lock{mtx_};
        if (completion_sent_) {
            return;
        }
        start_error_ = std::move(error);
        pending_ = 0;
        first_done_ = true;
        second_done_ = true;
        maybe_complete_locked(action);
    }

    template<std::size_t I>
    [[nodiscard]] auto is_done_locked() const noexcept -> bool {
        if constexpr (I == 0) {
            return first_done_;
        } else {
            return second_done_;
        }
    }

    template<std::size_t I>
    auto mark_done_locked() noexcept -> void {
        if constexpr (I == 0) {
            first_done_ = true;
        } else {
            second_done_ = true;
        }
    }

    template<std::size_t I>
    [[nodiscard]] auto result_slot_locked() noexcept -> auto& {
        if constexpr (I == 0) {
            return first_;
        } else {
            return second_;
        }
    }

    template<std::size_t I>
    auto select_winner_locked(terminal_kind kind) noexcept -> bool {
        if (winner_) {
            return false;
        }
        winner_ = I;
        winner_kind_ = kind;
        return true;
    }

    auto finish_child(completion_action& action) noexcept -> void {
        std::lock_guard lock{mtx_};
        // store_start_error force-zeroes pending_ while children may still
        // finish afterwards; guard the decrement so pending_ never
        // underflows (the underflow was masked by completion_sent_, but
        // keep the "children not yet accounted for" invariant honest).
        if (pending_ != 0) {
            --pending_;
        }
        maybe_complete_locked(action);
    }

    static auto set_result_action(
        io_status status,
        std::error_code error,
        payload_t&& payload,
        completion_action& action) -> void {
        action.kind = action_kind::value;
        if (status == io_status::error) {
            action.value.emplace(
                aggregate_t::failure(error, std::move(payload)));
        } else if (status == io_status::eof) {
            action.value.emplace(
                aggregate_t::end_of_file(std::move(payload)));
        } else {
            action.value.emplace(
                aggregate_t::success(std::move(payload)));
        }
    }

    auto maybe_complete_locked(completion_action& action) noexcept -> void {
        if (completion_sent_ || pending_ != 0) {
            return;
        }
        completion_sent_ = true;
        action.receiver = std::addressof(*receiver_);

        if (start_error_) {
            action.kind = action_kind::error;
            action.error = std::move(start_error_);
            return;
        }
        if (winner_kind_ == terminal_kind::error) {
            action.kind = action_kind::error;
            action.error = std::move(winner_error_);
            return;
        }
        if (winner_kind_ == terminal_kind::stopped || !winner_) {
            action.kind = action_kind::stopped;
            return;
        }

        try {
            payload_t payload{
                std::move(first_),
                std::move(second_),
                *winner_};
            if (*winner_ == 0) {
                const auto status = payload.first->status();
                const auto error = payload.first->error();
                set_result_action(
                    status,
                    error,
                    std::move(payload),
                    action);
            } else {
                const auto status = payload.second->status();
                const auto error = payload.second->error();
                set_result_action(
                    status,
                    error,
                    std::move(payload),
                    action);
            }
        } catch (...) {
            action.kind = action_kind::error;
            action.value.reset();
            action.error = std::current_exception();
        }
    }

    static auto deliver(completion_action& action) noexcept -> void {
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
    std::optional<std::size_t> winner_;
    terminal_kind winner_kind_ = terminal_kind::none;
    std::optional<First> first_;
    std::optional<Second> second_;
    std::exception_ptr winner_error_;
    std::exception_ptr start_error_;
};

template<class First, class Second>
class when_any_sender {
public:
    using sender_concept = std::execution::sender_t;
    using payload_t = when_any_result<First, Second>;
    using aggregate_t = io_result<payload_t>;

    when_any_sender(io_task<First> first, io_task<Second> second, io_env env)
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
        using operation_state_concept =
            std::execution::operation_state_t;
        using state_t = when_any_shared_state<First, Second, Receiver>;
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
            auto state = state_;
            try {
                receiver_stop_.install(
                    std::execution::get_stop_token(
                        std::execution::get_env(state->receiver())),
                    state->stop_source());
            } catch (...) {
                state->set_start_error(std::current_exception());
                return;
            }
            try {
                std::execution::start(first_op_);
            } catch (...) {
                state->template set_error<0>(std::current_exception());
                state->template set_stopped<1>();
                return;
            }
            try {
                std::execution::start(second_op_);
            } catch (...) {
                state->template set_error<1>(std::current_exception());
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

} // namespace forge::io

#endif // __cpp_impl_coroutine
