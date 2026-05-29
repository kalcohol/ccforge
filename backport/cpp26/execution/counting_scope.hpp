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
#include "detail/op_storage.hpp"
#include "env.hpp"
#include "start_detached.hpp"
#include "upon.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <mutex>
#include <type_traits>
#include <utility>

namespace std::execution {

template<class Assoc>
concept scope_association =
    std::movable<Assoc> &&
    std::is_nothrow_move_constructible_v<Assoc> &&
    std::is_nothrow_move_assignable_v<Assoc> &&
    std::default_initializable<Assoc> &&
    requires(const Assoc assoc) {
        { static_cast<bool>(assoc) } noexcept -> std::same_as<bool>;
        { assoc.try_associate() } -> std::same_as<Assoc>;
    };

namespace __forge_scope_detail {

struct __test_sender {
    using sender_concept = sender_t;

    template<class Self, class Env>
    static constexpr auto get_completion_signatures() noexcept
        -> completion_signatures<set_value_t()> {
        return {};
    }

    auto get_env() const noexcept -> empty_env {
        return {};
    }
};

} // namespace __forge_scope_detail

template<class Token>
concept scope_token =
    std::copyable<Token> &&
    requires(const Token token) {
        { token.try_associate() } -> scope_association;
        { token.wrap(std::declval<__forge_scope_detail::__test_sender>()) } -> sender_in<empty_env>;
    };

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
    class scope_association;

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
    friend class scope_association;

    [[nodiscard]] scope_association __try_associate() noexcept;

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
// scope_association — RAII handle for one scope association
// ──────────────────────────────────────────────────────────────────────────

class simple_counting_scope::scope_association {
public:
    scope_association() noexcept = default;

    scope_association(scope_association&& other) noexcept
        : __scope_(std::exchange(other.__scope_, nullptr))
    {}

    scope_association& operator=(scope_association&& other) noexcept {
        if (this != &other) {
            __release();
            __scope_ = std::exchange(other.__scope_, nullptr);
        }
        return *this;
    }

    scope_association(const scope_association&) = delete;
    scope_association& operator=(const scope_association&) = delete;

    ~scope_association() noexcept {
        __release();
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return __scope_ != nullptr;
    }

    [[nodiscard]] scope_association try_associate() const noexcept {
        if (!__scope_) return {};
        return __scope_->__try_associate();
    }

private:
    friend class simple_counting_scope;

    explicit scope_association(simple_counting_scope* scope) noexcept
        : __scope_(scope) {}

    void __release() noexcept {
        if (!__scope_) return;
        auto* scope = std::exchange(__scope_, nullptr);
        scope->__decrement();
    }

    simple_counting_scope* __scope_ = nullptr;
};

static_assert(std::execution::scope_association<simple_counting_scope::scope_association>);

inline auto simple_counting_scope::__try_associate() noexcept -> scope_association {
    if (is_closed()) return {};
    __increment();
    return scope_association{this};
}

// ──────────────────────────────────────────────────────────────────────────
// scope_token — interface for associating work with the scope
// ──────────────────────────────────────────────────────────────────────────

class simple_counting_scope::scope_token {
public:
    scope_token() noexcept = default;
    explicit scope_token(simple_counting_scope* scope) noexcept
        : __scope_(scope) {}

    [[nodiscard]] scope_association try_associate() const noexcept {
        if (!__scope_) return {};
        return __scope_->__try_associate();
    }

    // wrap(sndr): return a sender associated with this scope.
    // Forge acquires the association at operation start. If the scope is
    // already closed, the wrapped sender completes with stopped.
    template<sender S>
    [[nodiscard]] auto wrap(S sndr) const;

    // associate(sndr): compatibility spelling retained for existing callers.
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
    using association_t = simple_counting_scope::scope_association;

    struct __recv {
        using receiver_concept = receiver_t;

        R* __rcvr;

        template<class... Vs>
        void set_value(Vs&&... vs) && noexcept {
            std::execution::set_value(std::move(*__rcvr), static_cast<Vs&&>(vs)...);
        }

        template<class E>
        void set_error(E&& e) && noexcept {
            std::execution::set_error(std::move(*__rcvr), static_cast<E&&>(e));
        }

        void set_stopped() && noexcept {
            std::execution::set_stopped(std::move(*__rcvr));
        }

        auto get_env() const noexcept -> env_of_t<R> {
            return std::execution::get_env(*__rcvr);
        }
    };

    using inner_op_t = connect_result_t<S, __recv>;

    token_t __token;
    S __sndr;
    R __rcvr;
    association_t __association;
    __forge_detail::__op_storage<1024> __inner_storage;

    __associated_op(token_t token, S sndr, R rcvr)
        : __token(token)
        , __sndr(std::move(sndr))
        , __rcvr(std::move(rcvr))
    {}

    ~__associated_op() {
        __inner_storage.destroy();
        __association = {};
    }

    void start() & noexcept {
        __association = __token.try_associate();
        if (!__association) {
            std::execution::set_stopped(std::move(__rcvr));
            return;
        }

        try {
            auto* op = __inner_storage.template emplace_from<inner_op_t>([&]() -> inner_op_t {
                return std::execution::connect(
                    std::move(__sndr),
                    __recv{&__rcvr});
            });
            std::execution::start(*op);
        } catch (...) {
            std::execution::set_error(std::move(__rcvr), std::current_exception());
        }
    }
};

template<class S>
struct __associated_sender {
    using sender_concept = sender_t;
    using source_t = S;

    simple_counting_scope::scope_token __token;
    S __sndr;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept {
        using self_t = std::remove_cvref_t<Self>;
        using up_cs_t = decltype(std::execution::get_completion_signatures(
            std::declval<const typename self_t::source_t&>(),
            std::declval<Env>()));
        return __forge_meta::__concat_cs_t<up_cs_t, completion_signatures<set_stopped_t()>>{};
    }

    template<receiver R>
    auto connect(R r) &&
        -> __associated_op<S, R>
    {
        return __associated_op<S, R>{__token, std::move(__sndr), std::move(r)};
    }

    template<receiver R>
        requires std::copy_constructible<S>
    auto connect(R r) const&
        -> __associated_op<S, R>
    {
        return __associated_op<S, R>{__token, __sndr, std::move(r)};
    }

    auto get_env() const noexcept -> env_of_t<S> {
        return std::execution::get_env(__sndr);
    }
};

} // namespace __forge_counting_scope

template<sender S>
[[nodiscard]] auto simple_counting_scope::scope_token::wrap(S sndr) const {
    return __forge_counting_scope::__associated_sender<S>{*this, std::move(sndr)};
}

template<sender S>
[[nodiscard]] auto simple_counting_scope::scope_token::associate(S sndr) const {
    return wrap(std::move(sndr));
}

template<sender S>
void simple_counting_scope::scope_token::spawn(S sndr) {
    start_detached(wrap(std::move(sndr)) |
        upon_error([](auto&&) noexcept {}));
}

static_assert(std::execution::scope_token<simple_counting_scope::scope_token>);

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
