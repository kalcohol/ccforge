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
#include <condition_variable>
#include <exception>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <type_traits>
#include <utility>

namespace forge {

struct async_scope_options {
    std::pmr::memory_resource* memory = default_memory_resource();
};

namespace __async_scope_detail {

struct __state {
    explicit __state(std::pmr::memory_resource* memory_resource)
        : memory(normalize_memory_resource(memory_resource))
    {}

    bool try_acquire() {
        std::lock_guard lk{mtx};
        if (closed) {
            return false;
        }
        ++active;
        return true;
    }

    void complete(std::exception_ptr error = {}) noexcept {
        {
            std::lock_guard lk{mtx};
            if (error && !first_error) {
                first_error = std::move(error);
            }
            if (active > 0) {
                --active;
            }
            if (active == 0) {
                cv.notify_all();
            }
        }
    }

    void close() noexcept {
        std::lock_guard lk{mtx};
        closed = true;
    }

    void request_stop() noexcept {
        stop_source.request_stop();
    }

    void shutdown() noexcept {
        close();
        request_stop();
    }

    [[nodiscard]] bool is_closed() const noexcept {
        std::lock_guard lk{mtx};
        return closed;
    }

    [[nodiscard]] bool stop_requested() const noexcept {
        return stop_source.stop_requested();
    }

    [[nodiscard]] std::exception_ptr get_first_error() const noexcept {
        std::lock_guard lk{mtx};
        return first_error;
    }

    void wait() noexcept {
        std::unique_lock lk{mtx};
        cv.wait(lk, [this] { return active == 0; });
    }

    [[nodiscard]] auto memory_resource() const noexcept -> std::pmr::memory_resource* {
        return memory;
    }

    std::pmr::memory_resource* memory;
    mutable std::mutex mtx;
    std::condition_variable cv;
    std::size_t active = 0;
    bool closed = false;
    std::exception_ptr first_error{};
    std::inplace_stop_source stop_source{};
};

template<class T>
constexpr decltype(auto) __copy_or_move_lvalue(T&& value) noexcept {
    using value_t = std::remove_cvref_t<T>;
    if constexpr (std::is_lvalue_reference_v<T&&> &&
                  !std::is_const_v<std::remove_reference_t<T>> &&
                  !std::copy_constructible<value_t>) {
        return std::move(value);
    } else {
        return static_cast<T&&>(value);
    }
}

struct __env {
    std::shared_ptr<__state> state;

    friend auto tag_invoke(
        std::execution::get_stop_token_t,
        const __env& self) noexcept -> std::inplace_stop_token {
        return self.state->stop_source.get_token();
    }
};

template<class Node>
struct __receiver {
    using receiver_concept = std::execution::receiver_t;

    Node* node;

    template<class... Vs>
    void set_value(Vs&&...) && noexcept {
        node->complete({});
    }

    template<class Error>
    void set_error(Error&& error) && noexcept {
        node->complete(error_to_exception_ptr(static_cast<Error&&>(error)));
    }

    void set_stopped() && noexcept {
        node->complete({});
    }

    auto get_env() const noexcept -> __env {
        return __env{node->state()};
    }

private:
    static auto error_to_exception_ptr(std::exception_ptr error) noexcept
        -> std::exception_ptr {
        return error;
    }

    template<class Error>
    static auto error_to_exception_ptr(Error&& error) noexcept
        -> std::exception_ptr {
        try {
            return std::make_exception_ptr(static_cast<Error&&>(error));
        } catch (...) {
            return std::current_exception();
        }
    }
};

struct __op_node_base {
    explicit __op_node_base(std::pmr::memory_resource* memory) noexcept
        : memory_(normalize_memory_resource(memory))
    {}

    __op_node_base(const __op_node_base&) = delete;
    __op_node_base& operator=(const __op_node_base&) = delete;

    void add_ref() noexcept {
        refs.fetch_add(1, std::memory_order_relaxed);
    }

    void release() noexcept {
        if (refs.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            destroy_self();
        }
    }

    void start() noexcept {
        add_ref();
        start_impl();
        release();
    }

    virtual void start_impl() noexcept = 0;
    virtual void destroy_self() noexcept = 0;

protected:
    virtual ~__op_node_base() = default;

    [[nodiscard]] auto memory_resource() const noexcept -> std::pmr::memory_resource* {
        return memory_;
    }

private:
    std::pmr::memory_resource* memory_;
    std::atomic<unsigned> refs{1};
};

template<class S>
struct __op_node final : __op_node_base {
    using sender_t = S;
    using receiver_t = __receiver<__op_node>;
    using op_t = std::execution::connect_result_t<sender_t, receiver_t>;

    __op_node(
        std::pmr::memory_resource* memory,
        std::shared_ptr<__state> st,
        S sndr)
        : __op_node_base(memory)
        , state_(std::move(st))
        , sender_(std::move(sndr))
        , op_(std::execution::connect(std::move(sender_), receiver_t{this}))
    {}

    void start_impl() noexcept override {
        std::execution::start(op_);
    }

    [[nodiscard]] auto state() const noexcept -> std::shared_ptr<__state> {
        return state_;
    }

    void complete(std::exception_ptr error) noexcept {
        if (completed_.exchange(true, std::memory_order_acq_rel)) {
            return;
        }

        auto st = state_;
        st->complete(std::move(error));
        release();
    }

    void destroy_self() noexcept override {
        auto* memory = memory_resource();
        std::pmr::polymorphic_allocator<__op_node> alloc{memory};
        std::destroy_at(this);
        alloc.deallocate(this, 1);
    }

private:
    std::shared_ptr<__state> state_;
    [[no_unique_address]] sender_t sender_;
    op_t op_;
    std::atomic<bool> completed_{false};
};

} // namespace __async_scope_detail

class async_scope {
public:
    async_scope()
        : async_scope(async_scope_options{})
    {}

    explicit async_scope(async_scope_options options)
        : state_(std::allocate_shared<__async_scope_detail::__state>(
              std::pmr::polymorphic_allocator<__async_scope_detail::__state>{
                  normalize_memory_resource(options.memory)},
              options.memory))
    {}

    ~async_scope() noexcept {
        shutdown();
        wait();
    }

    async_scope(const async_scope&) = delete;
    async_scope& operator=(const async_scope&) = delete;
    async_scope(async_scope&&) = delete;
    async_scope& operator=(async_scope&&) = delete;

    void request_stop() noexcept {
        state_->request_stop();
    }

    void close() noexcept {
        state_->close();
    }

    void shutdown() noexcept {
        state_->shutdown();
    }

    [[nodiscard]] bool stop_requested() const noexcept {
        return state_->stop_requested();
    }

    [[nodiscard]] bool closed() const noexcept {
        return state_->is_closed();
    }

    template<class S>
        requires std::execution::sender<std::remove_cvref_t<S>>
    bool spawn(S&& sender) {
        using sender_t = std::remove_cvref_t<S>;
        using node_t = __async_scope_detail::__op_node<sender_t>;

        auto st = state_;
        if (!st->try_acquire()) {
            return false;
        }

        auto* memory = st->memory_resource();
        std::pmr::polymorphic_allocator<node_t> alloc{memory};
        node_t* node = nullptr;
        try {
            node = alloc.allocate(1);
            std::construct_at(node,
                memory,
                st,
                sender_t(__async_scope_detail::__copy_or_move_lvalue(
                    static_cast<S&&>(sender))));
        } catch (...) {
            if (node != nullptr) {
                alloc.deallocate(node, 1);
            }
            st->complete(std::current_exception());
            return false;
        }

        node->start();
        return true;
    }

    void wait() noexcept {
        state_->wait();
    }

    [[nodiscard]] auto first_error() const noexcept -> std::exception_ptr {
        return state_->get_first_error();
    }

    void rethrow_if_error() const {
        if (auto error = first_error()) {
            std::rethrow_exception(error);
        }
    }

private:
    std::shared_ptr<__async_scope_detail::__state> state_;
};

} // namespace forge
