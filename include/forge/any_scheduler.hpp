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

#include "any_receiver.hpp"

#include <execution>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace forge {

class any_scheduler;

namespace __any_scheduler_detail {

using __schedule_completions = std::execution::completion_signatures<
    std::execution::set_value_t(),
    std::execution::set_error_t(std::exception_ptr),
    std::execution::set_stopped_t()>;

using __receiver = forge::any_receiver_of<__schedule_completions>;

struct __operation_base {
    virtual ~__operation_base() = default;
    virtual void start() noexcept = 0;
};

template<class Op>
struct __operation_model final : __operation_base {
    template<class Sender>
    __operation_model(Sender&& sndr, __receiver rcvr)
        : op_(std::execution::connect(static_cast<Sender&&>(sndr), std::move(rcvr))) {}

    void start() noexcept override {
        std::execution::start(op_);
    }

    Op op_;
};

struct __state_base {
    virtual ~__state_base() = default;
    virtual auto connect(__receiver rcvr) -> std::unique_ptr<__operation_base> = 0;
};

template<class Scheduler>
struct __state_model final : __state_base {
    template<class S>
    explicit __state_model(S&& scheduler)
        : scheduler_(static_cast<S&&>(scheduler)) {}

    auto connect(__receiver rcvr) -> std::unique_ptr<__operation_base> override {
        using sender_t = decltype(std::execution::schedule(scheduler_));
        using op_t = decltype(std::execution::connect(
            std::declval<sender_t>(),
            std::declval<__receiver>()));
        return std::make_unique<__operation_model<op_t>>(
            std::execution::schedule(scheduler_),
            std::move(rcvr));
    }

    Scheduler scheduler_;
};

template<class R>
struct __op {
    using operation_state_concept = std::execution::operation_state_t;

    __op(__op&&) = delete;
    __op& operator=(__op&&) = delete;
    __op(const __op&) = delete;
    __op& operator=(const __op&) = delete;

    __op(std::shared_ptr<__state_base> state, R rcvr)
        : state_(std::move(state)) {
        if (!state_) {
            rcvr_.emplace(std::move(rcvr));
            error_ = std::make_exception_ptr(
                std::runtime_error("any_scheduler: empty"));
            return;
        }

        op_ = state_->connect(__receiver{std::move(rcvr)});
    }

    void start() & noexcept {
        if (op_) {
            op_->start();
            return;
        }

        std::execution::set_error(std::move(*rcvr_), error_);
    }

    std::shared_ptr<__state_base> state_;
    std::unique_ptr<__operation_base> op_;
    std::optional<R> rcvr_;
    std::exception_ptr error_;
};

struct __sender {
    using sender_concept = std::execution::sender_t;

    std::shared_ptr<__state_base> state;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept -> __schedule_completions {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }

    template<class R>
        requires std::execution::receiver_of<R, __schedule_completions>
    auto connect(R rcvr) && -> __op<R> {
        return __op<R>{std::move(state), std::move(rcvr)};
    }

    template<class R>
        requires std::execution::receiver_of<R, __schedule_completions>
    auto connect(R rcvr) const& -> __op<R> {
        return __op<R>{state, std::move(rcvr)};
    }
};

} // namespace __any_scheduler_detail

class any_scheduler {
public:
    using scheduler_concept = std::execution::scheduler_t;

    any_scheduler() = default;

    template<class Scheduler>
        requires (!std::is_same_v<std::remove_cvref_t<Scheduler>, any_scheduler>)
              && std::execution::scheduler<std::remove_cvref_t<Scheduler>>
    any_scheduler(Scheduler&& scheduler)
        : state_(std::make_shared<
              __any_scheduler_detail::__state_model<std::remove_cvref_t<Scheduler>>>(
              static_cast<Scheduler&&>(scheduler))) {}

    any_scheduler(any_scheduler&&) noexcept = default;
    any_scheduler& operator=(any_scheduler&&) noexcept = default;
    any_scheduler(const any_scheduler&) noexcept = default;
    any_scheduler& operator=(const any_scheduler&) noexcept = default;
    ~any_scheduler() = default;

    [[nodiscard]] auto schedule() const noexcept
        -> __any_scheduler_detail::__sender {
        return __any_scheduler_detail::__sender{state_};
    }

    explicit operator bool() const noexcept {
        return static_cast<bool>(state_);
    }

    friend bool operator==(const any_scheduler& lhs, const any_scheduler& rhs) noexcept {
        return lhs.state_ == rhs.state_;
    }

private:
    std::shared_ptr<__any_scheduler_detail::__state_base> state_;
};

} // namespace forge
