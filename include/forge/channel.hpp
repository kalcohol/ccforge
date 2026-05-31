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

#include "resource_policy.hpp"

#include <execution>
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <deque>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace forge {

struct bounded_channel_options {
    std::size_t capacity = 0;
    std::pmr::memory_resource* memory = default_memory_resource();
};

namespace __channel_detail {

template<class R>
bool __stop_requested(const R& rcvr) noexcept {
    if constexpr (requires {
                      std::execution::get_stop_token(
                          std::execution::get_env(rcvr));
                  }) {
        return std::execution::get_stop_token(
            std::execution::get_env(rcvr)).stop_requested();
    } else {
        return false;
    }
}

template<class T>
struct __send_base {
    virtual ~__send_base() = default;
    virtual auto take_value() -> T = 0;
    virtual void complete_value() noexcept = 0;
    virtual void complete_stopped() noexcept = 0;
};

template<class T>
struct __recv_base {
    virtual ~__recv_base() = default;
    virtual void complete_value(T value) noexcept = 0;
    virtual void complete_stopped() noexcept = 0;
};

template<class T>
struct __actions {
    using send_ptr = std::shared_ptr<__send_base<T>>;
    using recv_ptr = std::shared_ptr<__recv_base<T>>;

    explicit __actions(std::pmr::memory_resource* memory)
        : send_value(memory)
        , send_stopped(memory)
        , recv_value(memory)
        , recv_stopped(memory)
    {}

    std::pmr::vector<send_ptr> send_value;
    std::pmr::vector<send_ptr> send_stopped;
    std::pmr::vector<std::pair<recv_ptr, T>> recv_value;
    std::pmr::vector<recv_ptr> recv_stopped;

    void run() noexcept {
        for (auto& item : recv_value) {
            item.first->complete_value(std::move(item.second));
        }
        for (auto& send : send_value) {
            send->complete_value();
        }
        for (auto& recv : recv_stopped) {
            recv->complete_stopped();
        }
        for (auto& send : send_stopped) {
            send->complete_stopped();
        }
    }
};

template<class T>
struct __state : std::enable_shared_from_this<__state<T>> {
    using send_ptr = std::shared_ptr<__send_base<T>>;
    using recv_ptr = std::shared_ptr<__recv_base<T>>;

    explicit __state(bounded_channel_options options)
        : capacity(options.capacity)
        , memory(normalize_memory_resource(options.memory))
        , buffer(memory)
        , pending_sends(memory)
        , pending_recvs(memory)
    {}

    [[nodiscard]] bool start_send(send_ptr send) {
        __actions<T> actions{memory};
        {
            std::lock_guard lk{mtx};
            if (stopped || closed) {
                actions.send_stopped.push_back(std::move(send));
            } else if (!pending_recvs.empty()) {
                auto recv = std::move(pending_recvs.front());
                pending_recvs.pop_front();
                actions.recv_value.emplace_back(std::move(recv), send->take_value());
                actions.send_value.push_back(std::move(send));
            } else if (buffer.size() < capacity) {
                buffer.push_back(send->take_value());
                actions.send_value.push_back(std::move(send));
            } else {
                pending_sends.push_back(std::move(send));
                return true;
            }
        }
        actions.run();
        return false;
    }

    [[nodiscard]] bool start_recv(recv_ptr recv) {
        __actions<T> actions{memory};
        {
            std::lock_guard lk{mtx};
            if (!buffer.empty()) {
                actions.recv_value.emplace_back(std::move(recv), std::move(buffer.front()));
                buffer.pop_front();
                promote_one_send_locked(actions);
            } else if (!pending_sends.empty()) {
                auto send = std::move(pending_sends.front());
                pending_sends.pop_front();
                actions.recv_value.emplace_back(std::move(recv), send->take_value());
                actions.send_value.push_back(std::move(send));
            } else if (stopped || closed) {
                actions.recv_stopped.push_back(std::move(recv));
            } else {
                pending_recvs.push_back(std::move(recv));
                return true;
            }
        }
        actions.run();
        return false;
    }

    bool try_send(T value) {
        __actions<T> actions{memory};
        bool accepted = false;
        {
            std::lock_guard lk{mtx};
            if (stopped || closed) {
                return false;
            }
            if (!pending_recvs.empty()) {
                auto recv = std::move(pending_recvs.front());
                pending_recvs.pop_front();
                actions.recv_value.emplace_back(std::move(recv), std::move(value));
                accepted = true;
            } else if (buffer.size() < capacity) {
                buffer.push_back(std::move(value));
                accepted = true;
            }
        }
        actions.run();
        return accepted;
    }

    auto try_recv() -> std::optional<T> {
        __actions<T> actions{memory};
        std::optional<T> result;
        {
            std::lock_guard lk{mtx};
            if (!buffer.empty()) {
                result.emplace(std::move(buffer.front()));
                buffer.pop_front();
                promote_one_send_locked(actions);
            } else if (!pending_sends.empty()) {
                auto send = std::move(pending_sends.front());
                pending_sends.pop_front();
                result.emplace(send->take_value());
                actions.send_value.push_back(std::move(send));
            }
        }
        actions.run();
        return result;
    }

    void close() noexcept {
        __actions<T> actions{memory};
        {
            std::lock_guard lk{mtx};
            closed = true;
            while (!pending_sends.empty()) {
                actions.send_stopped.push_back(std::move(pending_sends.front()));
                pending_sends.pop_front();
            }
            drain_buffer_to_receivers_locked(actions);
            if (buffer.empty()) {
                stop_pending_receivers_locked(actions);
            }
        }
        actions.run();
    }

    void request_stop() noexcept {
        __actions<T> actions{memory};
        std::pmr::deque<T> discarded{memory};
        {
            std::lock_guard lk{mtx};
            stopped = true;
            while (!pending_sends.empty()) {
                actions.send_stopped.push_back(std::move(pending_sends.front()));
                pending_sends.pop_front();
            }
            while (!pending_recvs.empty()) {
                actions.recv_stopped.push_back(std::move(pending_recvs.front()));
                pending_recvs.pop_front();
            }
            discarded.swap(buffer);
        }
        actions.run();
    }

    void shutdown() noexcept {
        close();
        request_stop();
    }

    void cancel_send(const send_ptr& send) noexcept {
        send_ptr stopped;
        {
            std::lock_guard lk{mtx};
            auto it = std::find(pending_sends.begin(), pending_sends.end(), send);
            if (it == pending_sends.end()) {
                return;
            }
            stopped = std::move(*it);
            pending_sends.erase(it);
        }
        stopped->complete_stopped();
    }

    void cancel_recv(const recv_ptr& recv) noexcept {
        recv_ptr stopped;
        {
            std::lock_guard lk{mtx};
            auto it = std::find(pending_recvs.begin(), pending_recvs.end(), recv);
            if (it == pending_recvs.end()) {
                return;
            }
            stopped = std::move(*it);
            pending_recvs.erase(it);
        }
        stopped->complete_stopped();
    }

    [[nodiscard]] bool is_closed() const noexcept {
        std::lock_guard lk{mtx};
        return closed;
    }

    void promote_one_send_locked(__actions<T>& actions) {
        if (pending_sends.empty()) {
            return;
        }
        if (!pending_recvs.empty()) {
            auto send = std::move(pending_sends.front());
            auto recv = std::move(pending_recvs.front());
            pending_sends.pop_front();
            pending_recvs.pop_front();
            actions.recv_value.emplace_back(std::move(recv), send->take_value());
            actions.send_value.push_back(std::move(send));
        } else if (buffer.size() < capacity) {
            auto send = std::move(pending_sends.front());
            pending_sends.pop_front();
            buffer.push_back(send->take_value());
            actions.send_value.push_back(std::move(send));
        }
    }

    void drain_buffer_to_receivers_locked(__actions<T>& actions) {
        while (!buffer.empty() && !pending_recvs.empty()) {
            auto recv = std::move(pending_recvs.front());
            pending_recvs.pop_front();
            actions.recv_value.emplace_back(std::move(recv), std::move(buffer.front()));
            buffer.pop_front();
        }
    }

    void stop_pending_receivers_locked(__actions<T>& actions) {
        while (!pending_recvs.empty()) {
            actions.recv_stopped.push_back(std::move(pending_recvs.front()));
            pending_recvs.pop_front();
        }
    }

    mutable std::mutex mtx;
    std::size_t capacity;
    std::pmr::memory_resource* memory;
    bool closed = false;
    bool stopped = false;
    std::pmr::deque<T> buffer;
    std::pmr::deque<send_ptr> pending_sends;
    std::pmr::deque<recv_ptr> pending_recvs;
};

template<class T, class R>
struct __send_record final : __send_base<T> {
    using state_t = __state<T>;
    using record_t = __send_record<T, R>;

    struct __stop_callback_fn {
        std::weak_ptr<state_t> state;
        std::weak_ptr<record_t> record;

        void operator()() noexcept {
            auto rec = record.lock();
            if (rec) {
                rec->stop_requested.store(true, std::memory_order_release);
            }
            auto st = state.lock();
            if (st && rec) {
                st->cancel_send(rec);
            }
        }
    };

    using callback_t = std::stop_callback_for_t<std::any_stop_token, __stop_callback_fn>;

    R rcvr;
    std::optional<T> value;
    std::optional<callback_t> stop_callback;
    std::atomic<bool> stop_requested{false};
    std::atomic<bool> done{false};

    __send_record(R r, T v)
        : rcvr(std::move(r)), value(std::move(v)) {}

    auto take_value() -> T override {
        return std::move(*value);
    }

    void complete_value() noexcept override {
        if (done.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        stop_callback.reset();
        value.reset();
        std::execution::set_value(std::move(rcvr));
    }

    void complete_stopped() noexcept override {
        if (done.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        stop_callback.reset();
        value.reset();
        std::execution::set_stopped(std::move(rcvr));
    }

    [[nodiscard]] bool install_stop_callback(
        const std::shared_ptr<state_t>& state,
        const std::shared_ptr<record_t>& self) noexcept {
        if (done.load(std::memory_order_acquire)) {
            return false;
        }
        if constexpr (requires {
                          std::any_stop_token{
                              std::execution::get_stop_token(
                                  std::execution::get_env(rcvr))};
                      }) {
            try {
                auto token = std::any_stop_token{
                    std::execution::get_stop_token(std::execution::get_env(rcvr))};
                if (token.stop_possible()) {
                    stop_callback.emplace(
                        std::move(token),
                        __stop_callback_fn{state, self});
                }
            } catch (...) {
                self->complete_stopped();
                return false;
            }
        }
        return true;
    }
};

template<class T, class R>
struct __recv_record final : __recv_base<T> {
    using state_t = __state<T>;
    using record_t = __recv_record<T, R>;

    struct __stop_callback_fn {
        std::weak_ptr<state_t> state;
        std::weak_ptr<record_t> record;

        void operator()() noexcept {
            auto rec = record.lock();
            if (rec) {
                rec->stop_requested.store(true, std::memory_order_release);
            }
            auto st = state.lock();
            if (st && rec) {
                st->cancel_recv(rec);
            }
        }
    };

    using callback_t = std::stop_callback_for_t<std::any_stop_token, __stop_callback_fn>;

    R rcvr;
    std::optional<callback_t> stop_callback;
    std::atomic<bool> stop_requested{false};
    std::atomic<bool> done{false};

    explicit __recv_record(R r) : rcvr(std::move(r)) {}

    void complete_value(T value) noexcept override {
        if (done.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        stop_callback.reset();
        std::execution::set_value(std::move(rcvr), std::move(value));
    }

    void complete_stopped() noexcept override {
        if (done.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        stop_callback.reset();
        std::execution::set_stopped(std::move(rcvr));
    }

    [[nodiscard]] bool install_stop_callback(
        const std::shared_ptr<state_t>& state,
        const std::shared_ptr<record_t>& self) noexcept {
        if (done.load(std::memory_order_acquire)) {
            return false;
        }
        if constexpr (requires {
                          std::any_stop_token{
                              std::execution::get_stop_token(
                                  std::execution::get_env(rcvr))};
                      }) {
            try {
                auto token = std::any_stop_token{
                    std::execution::get_stop_token(std::execution::get_env(rcvr))};
                if (token.stop_possible()) {
                    stop_callback.emplace(
                        std::move(token),
                        __stop_callback_fn{state, self});
                }
            } catch (...) {
                self->complete_stopped();
                return false;
            }
        }
        return true;
    }
};

template<class T, class R>
struct __send_op {
    using operation_state_concept = std::execution::operation_state_t;
    using state_t = __state<T>;
    using record_t = __send_record<T, R>;

    std::shared_ptr<state_t> state;
    std::shared_ptr<record_t> record;

    __send_op(std::shared_ptr<state_t> st, R rcvr, T value)
        : state(std::move(st))
        , record(std::allocate_shared<record_t>(
              std::pmr::polymorphic_allocator<record_t>{state->memory},
              std::move(rcvr),
              std::move(value)))
    {}

    __send_op(__send_op&&) = delete;
    __send_op& operator=(__send_op&&) = delete;
    __send_op(const __send_op&) = delete;
    __send_op& operator=(const __send_op&) = delete;

    void start() & noexcept {
        auto rec = record;
        if (__stop_requested(rec->rcvr)) {
            rec->complete_stopped();
            return;
        }
        if (!rec->install_stop_callback(state, rec)) {
            return;
        }
        if (state->start_send(rec)) {
            if (rec->stop_requested.load(std::memory_order_acquire)) {
                state->cancel_send(rec);
            }
        }
    }
};

template<class T, class R>
struct __recv_op {
    using operation_state_concept = std::execution::operation_state_t;
    using state_t = __state<T>;
    using record_t = __recv_record<T, R>;

    std::shared_ptr<state_t> state;
    std::shared_ptr<record_t> record;

    __recv_op(std::shared_ptr<state_t> st, R rcvr)
        : state(std::move(st))
        , record(std::allocate_shared<record_t>(
              std::pmr::polymorphic_allocator<record_t>{state->memory},
              std::move(rcvr)))
    {}

    __recv_op(__recv_op&&) = delete;
    __recv_op& operator=(__recv_op&&) = delete;
    __recv_op(const __recv_op&) = delete;
    __recv_op& operator=(const __recv_op&) = delete;

    void start() & noexcept {
        auto rec = record;
        if (__stop_requested(rec->rcvr)) {
            rec->complete_stopped();
            return;
        }
        if (!rec->install_stop_callback(state, rec)) {
            return;
        }
        if (state->start_recv(rec)) {
            if (rec->stop_requested.load(std::memory_order_acquire)) {
                state->cancel_recv(rec);
            }
        }
    }
};

template<class T>
struct __send_sender {
    using sender_concept = std::execution::sender_t;

    std::shared_ptr<__state<T>> state;
    std::optional<T> value;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<
            std::execution::set_value_t(),
            std::execution::set_stopped_t()> {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env { return {}; }

    template<std::execution::receiver R>
    auto connect(R rcvr) && -> __send_op<T, R> {
        return __send_op<T, R>{std::move(state), std::move(rcvr), std::move(*value)};
    }
};

template<class T>
struct __recv_sender {
    using sender_concept = std::execution::sender_t;

    std::shared_ptr<__state<T>> state;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<
            std::execution::set_value_t(T),
            std::execution::set_stopped_t()> {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env { return {}; }

    template<std::execution::receiver R>
    auto connect(R rcvr) && -> __recv_op<T, R> {
        return __recv_op<T, R>{std::move(state), std::move(rcvr)};
    }
};

} // namespace __channel_detail

template<class T>
class bounded_channel {
public:
    static_assert(std::move_constructible<T>,
                  "forge::bounded_channel<T> requires move-constructible T");

    explicit bounded_channel(std::size_t capacity)
        : bounded_channel(bounded_channel_options{capacity})
    {}

    explicit bounded_channel(bounded_channel_options options)
        : state_(__make_state(options))
    {}

    ~bounded_channel() noexcept {
        shutdown();
    }

    bounded_channel(const bounded_channel&) = delete;
    bounded_channel& operator=(const bounded_channel&) = delete;
    bounded_channel(bounded_channel&&) = delete;
    bounded_channel& operator=(bounded_channel&&) = delete;

    void close() noexcept {
        state_->close();
    }

    void request_stop() noexcept {
        state_->request_stop();
    }

    void shutdown() noexcept {
        state_->shutdown();
    }

    [[nodiscard]] bool closed() const noexcept {
        return state_->is_closed();
    }

    [[nodiscard]] std::size_t capacity() const noexcept {
        return state_->capacity;
    }

    auto async_send(T value) -> __channel_detail::__send_sender<T> {
        return __channel_detail::__send_sender<T>{state_, std::move(value)};
    }

    auto async_recv() -> __channel_detail::__recv_sender<T> {
        return __channel_detail::__recv_sender<T>{state_};
    }

    bool try_send(T value) {
        return state_->try_send(std::move(value));
    }

    auto try_recv() -> std::optional<T> {
        return state_->try_recv();
    }

private:
    using state_t = __channel_detail::__state<T>;

    static auto __make_state(bounded_channel_options options)
        -> std::shared_ptr<state_t> {
        options.memory = normalize_memory_resource(options.memory);
        return std::allocate_shared<state_t>(
            std::pmr::polymorphic_allocator<state_t>{options.memory},
            options);
    }

    std::shared_ptr<__channel_detail::__state<T>> state_;
};

} // namespace forge
