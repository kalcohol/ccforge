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
#include "env.hpp"
#include "start_detached.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <mutex>
#include <utility>

namespace std::execution {

// ──────────────────────────────────────────────────────────────────────────
// simple_counting_scope — [exec.counting.scope.simple]
// P3149R11: Structured concurrency with async_scope
//
// simple_counting_scope allows spawning async work and joining (waiting)
// for all spawned work to complete. The count tracks outstanding operations.
// ──────────────────────────────────────────────────────────────────────────

class simple_counting_scope {
public:
    class scope_token;

    simple_counting_scope() noexcept = default;
    ~simple_counting_scope() noexcept {
        // Destructor: if count > 0, work was not properly joined.
        // In P3149R11, this is an error. We terminate.
        if (__count_.load(std::memory_order_acquire) != 0) {
            std::terminate();
        }
    }
    simple_counting_scope(const simple_counting_scope&) = delete;
    simple_counting_scope& operator=(const simple_counting_scope&) = delete;
    simple_counting_scope(simple_counting_scope&&) = delete;
    simple_counting_scope& operator=(simple_counting_scope&&) = delete;

    // Get the token for associating work with this scope
    [[nodiscard]] scope_token get_token() noexcept;

    // Close: prevent new work from being associated
    void close() noexcept {
        __closed_.store(true, std::memory_order_release);
    }

    // join: block until all associated work completes
    void join() {
        std::unique_lock lk{__mtx_};
        __cv_.wait(lk, [this] {
            return __count_.load(std::memory_order_acquire) == 0;
        });
    }

    [[nodiscard]] bool is_closed() const noexcept {
        return __closed_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::size_t count() const noexcept {
        return __count_.load(std::memory_order_acquire);
    }

private:
    friend class scope_token;

    void __increment() noexcept {
        __count_.fetch_add(1, std::memory_order_relaxed);
    }

    void __decrement() noexcept {
        if (__count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            // Last operation completed
            std::lock_guard lk{__mtx_};
            __cv_.notify_all();
        }
    }

    std::atomic<std::size_t> __count_{0};
    std::atomic<bool> __closed_{false};
    std::mutex __mtx_;
    std::condition_variable __cv_;
};

// ──────────────────────────────────────────────────────────────────────────
// scope_token — interface for associating work with the scope
// ──────────────────────────────────────────────────────────────────────────

class simple_counting_scope::scope_token {
public:
    scope_token() noexcept = default;
    explicit scope_token(simple_counting_scope* scope) noexcept
        : __scope_(scope) {}

    [[nodiscard]] bool try_associate() const noexcept {
        if (!__scope_ || __scope_->is_closed()) return false;
        __scope_->__increment();
        return true;
    }

    void disassociate() const noexcept {
        if (__scope_) __scope_->__decrement();
    }

    // associate(sndr): wrap sender so it decrements scope on completion
    // Returns a sender that, when started, increments the scope count,
    // runs sndr, and decrements on any completion channel.
    // If scope is closed, returns just_stopped().
    template<sender S>
    [[nodiscard]] auto associate(S sndr) const;

    // spawn(sndr): fire-and-forget, associated with this scope
    template<sender S>
    void spawn(S sndr);

private:
    simple_counting_scope* __scope_ = nullptr;
};

namespace __forge_counting_scope {

template<class S, class R>
struct __associated_op : __forge_detail::__immovable {
    using operation_state_concept = operation_state_t;

    using token_t = simple_counting_scope::scope_token;

    struct __recv {
        using receiver_concept = receiver_t;

        R* __rcvr;
        token_t __token;
        bool* __associated;

        void __done() noexcept {
            if (*__associated) {
                __token.disassociate();
                *__associated = false;
            }
        }

        template<class... Vs>
        friend void tag_invoke(set_value_t, __recv&& self, Vs&&... vs) noexcept {
            self.__done();
            set_value(std::move(*self.__rcvr), static_cast<Vs&&>(vs)...);
        }

        template<class E>
        friend void tag_invoke(set_error_t, __recv&& self, E&& e) noexcept {
            self.__done();
            set_error(std::move(*self.__rcvr), static_cast<E&&>(e));
        }

        friend void tag_invoke(set_stopped_t, __recv&& self) noexcept {
            self.__done();
            set_stopped(std::move(*self.__rcvr));
        }

        friend auto tag_invoke(get_env_t, const __recv& self) noexcept
            -> env_of_t<R> {
            return std::execution::get_env(*self.__rcvr);
        }
    };

    using inner_op_t = connect_result_t<S, __recv>;
    static_assert(sizeof(inner_op_t) <= 1024,
        "counting_scope::associate: inner op too large for buffer");

    token_t __token;
    S __sndr;
    R __rcvr;
    alignas(std::max_align_t) unsigned char __buf[1024];
    bool __inner_alive = false;
    bool __associated = false;

    __associated_op(token_t token, S sndr, R rcvr)
        : __token(token)
        , __sndr(std::move(sndr))
        , __rcvr(std::move(rcvr))
    {}

    ~__associated_op() {
        if (__inner_alive) {
            static_cast<inner_op_t*>(static_cast<void*>(__buf))->~inner_op_t();
        }
        if (__associated) {
            __token.disassociate();
        }
    }

    friend void tag_invoke(start_t, __associated_op& self) noexcept {
        if (!self.__token.try_associate()) {
            set_stopped(std::move(self.__rcvr));
            return;
        }

        self.__associated = true;
        try {
            ::new(static_cast<void*>(self.__buf)) inner_op_t(
                std::execution::connect(
                    std::move(self.__sndr),
                    __recv{&self.__rcvr, self.__token, &self.__associated}));
            self.__inner_alive = true;
            std::execution::start(*static_cast<inner_op_t*>(static_cast<void*>(self.__buf)));
        } catch (...) {
            if (self.__associated) {
                self.__token.disassociate();
                self.__associated = false;
            }
            set_error(std::move(self.__rcvr), std::current_exception());
        }
    }
};

template<class S>
struct __associated_sender {
    using sender_concept = sender_t;

    simple_counting_scope::scope_token __token;
    S __sndr;

    friend auto tag_invoke(get_completion_signatures_t,
                           const __associated_sender& self, auto env) noexcept {
        using up_cs_t = decltype(std::execution::get_completion_signatures(self.__sndr, env));
        return __forge_meta::__concat_cs_t<up_cs_t, completion_signatures<set_stopped_t()>>{};
    }

    template<receiver R>
    friend auto tag_invoke(connect_t, __associated_sender self, R r)
        -> __associated_op<S, R>
    {
        return __associated_op<S, R>{self.__token, std::move(self.__sndr), std::move(r)};
    }

    friend auto tag_invoke(get_env_t, const __associated_sender& self) noexcept {
        return std::execution::get_env(self.__sndr);
    }
};

} // namespace __forge_counting_scope

template<sender S>
[[nodiscard]] auto simple_counting_scope::scope_token::associate(S sndr) const {
    return __forge_counting_scope::__associated_sender<S>{*this, std::move(sndr)};
}

template<sender S>
void simple_counting_scope::scope_token::spawn(S sndr) {
    start_detached(associate(std::move(sndr)));
}

inline simple_counting_scope::scope_token
simple_counting_scope::get_token() noexcept {
    return scope_token{this};
}

// ──────────────────────────────────────────────────────────────────────────
// counting_scope — [exec.counting.scope]
// Extends simple_counting_scope with nestable token support.
// For Phase 3, counting_scope is equivalent to simple_counting_scope.
// Full P3149R11 nesting support can be added later.
// ──────────────────────────────────────────────────────────────────────────

using counting_scope = simple_counting_scope;

} // namespace std::execution
