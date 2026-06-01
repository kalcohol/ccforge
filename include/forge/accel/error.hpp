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
#include <exception>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace forge::accel {

enum class command_status {
    ok,
    failed,
    stopped,
    timed_out,
    aborted
};

class command_error : public std::runtime_error {
public:
    explicit command_error(command_status status)
        : std::runtime_error("forge::accel command failed")
        , status_(status) {}

    [[nodiscard]] auto status() const noexcept -> command_status {
        return status_;
    }

private:
    command_status status_;
};

enum class error_kind {
    unknown,
    invalid_context,
    invalid_buffer,
    invalid_memory_kind,
    size_mismatch,
    coherence_required,
    invalid_event,
    command_failed,
    timeout,
    aborted,
    user_exception
};

struct error {
    error_kind kind = error_kind::unknown;
    command_status status = command_status::failed;
    std::exception_ptr cause = nullptr;
};

class operation_error : public std::runtime_error {
public:
    operation_error(error_kind kind, const char* what)
        : std::runtime_error(what)
        , kind_(kind) {}

    operation_error(error_kind kind, command_status status, const char* what)
        : std::runtime_error(what)
        , kind_(kind)
        , status_(status) {}

    [[nodiscard]] auto kind() const noexcept -> error_kind {
        return kind_;
    }

    [[nodiscard]] auto status() const noexcept -> command_status {
        return status_;
    }

private:
    error_kind kind_ = error_kind::unknown;
    command_status status_ = command_status::failed;
};

namespace __typed_detail {

[[nodiscard]] inline auto from_exception(std::exception_ptr ep) noexcept -> error {
    if (!ep) {
        return {};
    }

    try {
        std::rethrow_exception(ep);
    } catch (const operation_error& e) {
        return error{e.kind(), e.status(), std::move(ep)};
    } catch (const command_error& e) {
        if (e.status() == command_status::timed_out) {
            return error{error_kind::timeout, e.status(), std::move(ep)};
        }
        if (e.status() == command_status::aborted) {
            return error{error_kind::aborted, e.status(), std::move(ep)};
        }
        return error{error_kind::command_failed, e.status(), std::move(ep)};
    } catch (...) {
        return error{error_kind::user_exception, command_status::failed, std::move(ep)};
    }
}

template<class R>
struct __void_receiver {
    using receiver_concept = std::execution::receiver_t;

    R rcvr;

    void set_value() && noexcept {
        std::execution::set_value(std::move(rcvr));
    }

    void set_error(std::exception_ptr ep) && noexcept {
        std::execution::set_error(std::move(rcvr), from_exception(std::move(ep)));
    }

    void set_stopped() && noexcept {
        std::execution::set_stopped(std::move(rcvr));
    }

    auto get_env() const noexcept(noexcept(std::execution::get_env(rcvr)))
        -> decltype(std::execution::get_env(rcvr)) {
        return std::execution::get_env(rcvr);
    }
};

template<class Sender>
struct __void_sender {
    using sender_concept = std::execution::sender_t;
    using completion_signatures = std::execution::completion_signatures<
        std::execution::set_value_t(),
        std::execution::set_error_t(error),
        std::execution::set_stopped_t()>;

    Sender sender;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept -> completion_signatures {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }

    template<class R>
        requires std::execution::receiver_of<R, completion_signatures>
    struct __op {
        using receiver_t = __void_receiver<R>;
        using op_t = std::execution::connect_result_t<Sender, receiver_t>;
        struct state_t {
            template<class Factory>
            explicit state_t(Factory&& factory)
                : op(static_cast<Factory&&>(factory)()) {}

            op_t op;
        };

        __op(Sender sender, R rcvr)
            : state(std::make_shared<state_t>([&]() -> op_t {
                  return std::execution::connect(
                      std::move(sender),
                      receiver_t{std::move(rcvr)});
              }))
        {}

        __op(__op&&) = delete;
        auto operator=(__op&&) -> __op& = delete;
        __op(const __op&) = delete;
        auto operator=(const __op&) -> __op& = delete;

        void start() & noexcept {
            auto keepalive = state;
            std::execution::start(keepalive->op);
        }

        std::shared_ptr<state_t> state;
    };

    template<class R>
        requires std::execution::receiver_of<R, completion_signatures>
    auto connect(R rcvr) && -> __op<R> {
        return __op<R>{std::move(sender), std::move(rcvr)};
    }

    template<class R>
        requires std::execution::receiver_of<R, completion_signatures>
              && std::copy_constructible<Sender>
    auto connect(R rcvr) const& -> __op<R> {
        return __op<R>{Sender(sender), std::move(rcvr)};
    }
};

template<class Sender>
[[nodiscard]] auto void_sender(Sender&& sender)
    -> __void_sender<std::decay_t<Sender>> {
    return __void_sender<std::decay_t<Sender>>{static_cast<Sender&&>(sender)};
}

template<class R>
struct __value_receiver {
    using receiver_concept = std::execution::receiver_t;

    R rcvr;

    template<class... Args>
    void set_value(Args&&... args) && noexcept {
        std::execution::set_value(std::move(rcvr), static_cast<Args&&>(args)...);
    }

    void set_error(std::exception_ptr ep) && noexcept {
        std::execution::set_error(std::move(rcvr), from_exception(std::move(ep)));
    }

    void set_error(error e) && noexcept {
        std::execution::set_error(std::move(rcvr), std::move(e));
    }

    template<class E>
    void set_error(E&& e) && noexcept {
        std::execution::set_error(
            std::move(rcvr),
            from_exception(std::make_exception_ptr(static_cast<E&&>(e))));
    }

    void set_stopped() && noexcept {
        std::execution::set_stopped(std::move(rcvr));
    }

    auto get_env() const noexcept(noexcept(std::execution::get_env(rcvr)))
        -> decltype(std::execution::get_env(rcvr)) {
        return std::execution::get_env(rcvr);
    }
};

template<class Sender, class Value>
struct __value_sender {
    using sender_concept = std::execution::sender_t;
    using completion_signatures = std::execution::completion_signatures<
        std::execution::set_value_t(Value),
        std::execution::set_error_t(error),
        std::execution::set_stopped_t()>;

    Sender sender;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept -> completion_signatures {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }

    template<class R>
        requires std::execution::receiver_of<R, completion_signatures>
    struct __op {
        using receiver_t = __value_receiver<R>;
        using op_t = std::execution::connect_result_t<Sender, receiver_t>;
        struct state_t {
            template<class Factory>
            explicit state_t(Factory&& factory)
                : op(static_cast<Factory&&>(factory)()) {}

            op_t op;
        };

        __op(Sender sender, R rcvr)
            : state(std::make_shared<state_t>([&]() -> op_t {
                  return std::execution::connect(
                      std::move(sender),
                      receiver_t{std::move(rcvr)});
              }))
        {}

        __op(__op&&) = delete;
        auto operator=(__op&&) -> __op& = delete;
        __op(const __op&) = delete;
        auto operator=(const __op&) -> __op& = delete;

        void start() & noexcept {
            auto keepalive = state;
            std::execution::start(keepalive->op);
        }

        std::shared_ptr<state_t> state;
    };

    template<class R>
        requires std::execution::receiver_of<R, completion_signatures>
    auto connect(R rcvr) && -> __op<R> {
        return __op<R>{std::move(sender), std::move(rcvr)};
    }

    template<class R>
        requires std::execution::receiver_of<R, completion_signatures>
              && std::copy_constructible<Sender>
    auto connect(R rcvr) const& -> __op<R> {
        return __op<R>{Sender(sender), std::move(rcvr)};
    }
};

template<class Value, class Sender>
[[nodiscard]] auto value_sender(Sender&& sender)
    -> __value_sender<std::decay_t<Sender>, Value> {
    return __value_sender<std::decay_t<Sender>, Value>{
        static_cast<Sender&&>(sender)};
}

} // namespace __typed_detail

} // namespace forge::accel
