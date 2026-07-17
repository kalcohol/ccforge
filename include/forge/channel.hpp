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
#include <atomic>
#include <cstddef>
#include <exception>
#include <list>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>

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
    std::shared_ptr<__send_base> next;

    virtual ~__send_base() = default;
    virtual auto take_value() -> T = 0;
    virtual bool stop_requested() const noexcept = 0;
    virtual void complete_value() noexcept = 0;
    virtual void complete_error(std::exception_ptr error) noexcept = 0;
    virtual void complete_stopped() noexcept = 0;
};

template<class T>
struct __recv_base {
    std::shared_ptr<__recv_base> next;

    virtual ~__recv_base() = default;
    virtual bool stop_requested() const noexcept = 0;
    virtual void prepare_value(T value) noexcept = 0;
    virtual bool has_prepared_value() const noexcept = 0;
    virtual void complete_value() noexcept = 0;
    virtual void complete_stopped() noexcept = 0;
};

template<class Record>
struct __record_queue {
    using pointer = std::shared_ptr<Record>;

    [[nodiscard]] bool empty() const noexcept { return !head; }
    [[nodiscard]] auto front() const noexcept -> const pointer& { return head; }

    void push_back(pointer record) noexcept {
        record->next.reset();
        if (tail) {
            tail->next = record;
        } else {
            head = record;
        }
        tail = std::move(record);
    }

    auto pop_front() noexcept -> pointer {
        auto record = std::move(head);
        if (record) {
            head = std::move(record->next);
            if (!head) {
                tail.reset();
            }
        }
        return record;
    }

    auto remove(const pointer& record) noexcept -> pointer {
        pointer* link = &head;
        pointer previous;
        while (*link && *link != record) {
            previous = *link;
            link = &(*link)->next;
        }
        if (!*link) {
            return {};
        }

        auto removed = std::move(*link);
        *link = std::move(removed->next);
        if (!*link) {
            tail = std::move(previous);
        }
        return removed;
    }

    auto take_all() noexcept -> pointer {
        tail.reset();
        return std::move(head);
    }

    pointer head;
    pointer tail;
};

template<class T>
struct __actions {
    using send_ptr = std::shared_ptr<__send_base<T>>;
    using recv_ptr = std::shared_ptr<__recv_base<T>>;

    send_ptr send_value;
    send_ptr send_error;
    send_ptr send_stopped;
    recv_ptr recv_value;
    recv_ptr recv_stopped;
    std::exception_ptr send_exception;

    void run() noexcept {
        if (recv_value) {
            recv_value->complete_value();
        }
        if (send_value) {
            send_value->complete_value();
        }
        if (send_error) {
            send_error->complete_error(std::move(send_exception));
        }
        if (recv_stopped) {
            recv_stopped->complete_stopped();
        }
        if (send_stopped) {
            send_stopped->complete_stopped();
        }
    }
};

template<class T>
struct __state : std::enable_shared_from_this<__state<T>> {
    using send_ptr = std::shared_ptr<__send_base<T>>;
    using recv_ptr = std::shared_ptr<__recv_base<T>>;
    using buffer_t = std::pmr::list<T>;
    using send_queue_t = __record_queue<__send_base<T>>;
    using recv_queue_t = __record_queue<__recv_base<T>>;

    explicit __state(bounded_channel_options options)
        : capacity(options.capacity)
        , memory(normalize_memory_resource(options.memory))
        , buffer(memory)
    {}

    [[nodiscard]] bool start_send(send_ptr send) noexcept {
        __actions<T> actions;
        {
            std::lock_guard lk{mtx};
            if (stopped || closed) {
                actions.send_stopped = std::move(send);
            } else if (send->stop_requested()) {
                actions.send_stopped = std::move(send);
            } else if (!pending_recvs.empty()) {
                auto recv = pending_recvs.pop_front();
                recv->prepare_value(send->take_value());
                actions.recv_value = std::move(recv);
                actions.send_value = std::move(send);
            } else if (buffer.size() < capacity) {
                try {
                    buffer.push_back(send->take_value());
                    actions.send_value = std::move(send);
                } catch (...) {
                    actions.send_exception = std::current_exception();
                    actions.send_error = std::move(send);
                }
            } else {
                pending_sends.push_back(send);
                if (!send->stop_requested()) {
                    return true;
                }
                actions.send_stopped = pending_sends.remove(send);
            }
        }
        actions.run();
        return false;
    }

    [[nodiscard]] bool start_recv(recv_ptr recv) noexcept {
        __actions<T> actions;
        {
            std::lock_guard lk{mtx};
            if (!buffer.empty()) {
                recv->prepare_value(std::move(buffer.front()));
                buffer.pop_front();
                actions.recv_value = std::move(recv);
                promote_one_send_locked(actions);
            } else if (!pending_sends.empty()) {
                auto send = pending_sends.pop_front();
                recv->prepare_value(send->take_value());
                actions.recv_value = std::move(recv);
                actions.send_value = std::move(send);
            } else if (stopped || closed) {
                actions.recv_stopped = std::move(recv);
            } else if (recv->stop_requested()) {
                actions.recv_stopped = std::move(recv);
            } else {
                pending_recvs.push_back(recv);
                if (!recv->stop_requested()) {
                    return true;
                }
                actions.recv_stopped = pending_recvs.remove(recv);
            }
        }
        actions.run();
        return false;
    }

    bool try_send(T value) {
        __actions<T> actions;
        bool accepted = false;
        {
            std::lock_guard lk{mtx};
            if (stopped || closed) {
                return false;
            }
            if (!pending_recvs.empty()) {
                auto recv = pending_recvs.pop_front();
                recv->prepare_value(std::move(value));
                actions.recv_value = std::move(recv);
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
        __actions<T> actions;
        std::optional<T> result;
        {
            std::lock_guard lk{mtx};
            if (!buffer.empty()) {
                result.emplace(std::move(buffer.front()));
                buffer.pop_front();
                promote_one_send_locked(actions);
            } else if (!pending_sends.empty()) {
                auto send = pending_sends.pop_front();
                result.emplace(send->take_value());
                actions.send_value = std::move(send);
            }
        }
        actions.run();
        return result;
    }

    void close() noexcept {
        send_ptr sends;
        recv_ptr recvs;
        {
            std::lock_guard lk{mtx};
            closed = true;
            auto recv = pending_recvs.front();
            while (recv && !buffer.empty()) {
                recv->prepare_value(std::move(buffer.front()));
                buffer.pop_front();
                recv = recv->next;
            }
            sends = pending_sends.take_all();
            recvs = pending_recvs.take_all();
        }

        while (recvs) {
            auto next = std::move(recvs->next);
            if (recvs->has_prepared_value()) {
                recvs->complete_value();
            } else {
                recvs->complete_stopped();
            }
            recvs = std::move(next);
        }
        while (sends) {
            auto next = std::move(sends->next);
            sends->complete_stopped();
            sends = std::move(next);
        }
    }

    void request_stop() noexcept {
        send_ptr sends;
        recv_ptr recvs;
        buffer_t discarded{memory};
        {
            std::lock_guard lk{mtx};
            stopped = true;
            sends = pending_sends.take_all();
            recvs = pending_recvs.take_all();
            discarded.splice(discarded.end(), buffer);
        }

        while (recvs) {
            auto next = std::move(recvs->next);
            recvs->complete_stopped();
            recvs = std::move(next);
        }
        while (sends) {
            auto next = std::move(sends->next);
            sends->complete_stopped();
            sends = std::move(next);
        }
    }

    void shutdown() noexcept {
        close();
        request_stop();
    }

    void cancel_send(const send_ptr& send) noexcept {
        send_ptr stopped;
        {
            std::lock_guard lk{mtx};
            stopped = pending_sends.remove(send);
        }
        if (stopped) {
            stopped->complete_stopped();
        }
    }

    void cancel_recv(const recv_ptr& recv) noexcept {
        recv_ptr stopped;
        {
            std::lock_guard lk{mtx};
            stopped = pending_recvs.remove(recv);
        }
        if (stopped) {
            stopped->complete_stopped();
        }
    }

    [[nodiscard]] bool is_closed() const noexcept {
        std::lock_guard lk{mtx};
        return closed;
    }

    void promote_one_send_locked(__actions<T>& actions) noexcept {
        if (pending_sends.empty()) {
            return;
        }
        if (buffer.size() < capacity) {
            auto send = pending_sends.pop_front();
            try {
                buffer.push_back(send->take_value());
                actions.send_value = std::move(send);
            } catch (...) {
                actions.send_exception = std::current_exception();
                actions.send_error = std::move(send);
            }
        }
    }

    mutable std::mutex mtx;
    std::size_t capacity;
    std::pmr::memory_resource* memory;
    bool closed = false;
    bool stopped = false;
    buffer_t buffer;
    send_queue_t pending_sends;
    recv_queue_t pending_recvs;
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
                rec->stop_requested_flag.store(true, std::memory_order_release);
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
    std::atomic<bool> stop_requested_flag{false};
    std::atomic<bool> done{false};

    __send_record(R r, T v)
        : rcvr(std::move(r)), value(std::move(v)) {}

    auto take_value() -> T override {
        return std::move(*value);
    }

    bool stop_requested() const noexcept override {
        return stop_requested_flag.load(std::memory_order_acquire);
    }

    void complete_value() noexcept override {
        if (done.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        stop_callback.reset();
        value.reset();
        std::execution::set_value(std::move(rcvr));
    }

    void complete_error(std::exception_ptr error) noexcept override {
        if (done.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        stop_callback.reset();
        value.reset();
        std::execution::set_error(std::move(rcvr), std::move(error));
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
                rec->stop_requested_flag.store(true, std::memory_order_release);
            }
            auto st = state.lock();
            if (st && rec) {
                st->cancel_recv(rec);
            }
        }
    };

    using callback_t = std::stop_callback_for_t<std::any_stop_token, __stop_callback_fn>;

    R rcvr;
    std::optional<T> value;
    std::optional<callback_t> stop_callback;
    std::atomic<bool> stop_requested_flag{false};
    std::atomic<bool> done{false};

    explicit __recv_record(R r) : rcvr(std::move(r)) {}

    bool stop_requested() const noexcept override {
        return stop_requested_flag.load(std::memory_order_acquire);
    }

    void prepare_value(T next) noexcept override {
        value.emplace(std::move(next));
    }

    bool has_prepared_value() const noexcept override {
        return value.has_value();
    }

    void complete_value() noexcept override {
        if (done.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        stop_callback.reset();
        std::execution::set_value(std::move(rcvr), std::move(*value));
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
        auto st = state;
        auto rec = record;
        if (__stop_requested(rec->rcvr)) {
            rec->complete_stopped();
            return;
        }
        if (!rec->install_stop_callback(st, rec)) {
            return;
        }
        (void)st->start_send(rec);
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
        auto st = state;
        auto rec = record;
        if (__stop_requested(rec->rcvr)) {
            rec->complete_stopped();
            return;
        }
        if (!rec->install_stop_callback(st, rec)) {
            return;
        }
        (void)st->start_recv(rec);
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
            std::execution::set_error_t(std::exception_ptr),
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
    static_assert(std::is_nothrow_move_constructible_v<T>,
                  "forge::bounded_channel<T> requires nothrow-move-constructible T");

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
