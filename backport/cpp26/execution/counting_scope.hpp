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

#include <atomic>
#include <cstddef>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>

namespace std::execution {

namespace __forge_counting_scope {

struct __join_state_base {
    explicit __join_state_base(
        void (*complete)(__join_state_base*) noexcept) noexcept
        : __complete(complete) {}

    __join_state_base* __next = nullptr;
    bool __registered = false;
    void (*__complete)(__join_state_base*) noexcept = nullptr;
};

enum class __scope_state : std::uint8_t {
    unused,
    unused_and_closed,
    open,
    open_and_joining,
    closed,
    closed_and_joining,
    joined,
};

constexpr __scope_state __scope_after_close(__scope_state state) noexcept {
    switch (state) {
    case __scope_state::unused:
        return __scope_state::unused_and_closed;
    case __scope_state::open:
        return __scope_state::closed;
    case __scope_state::open_and_joining:
        return __scope_state::closed_and_joining;
    default:
        return state;
    }
}

constexpr __scope_state __scope_after_association(__scope_state state) noexcept {
    return state == __scope_state::unused ? __scope_state::open : state;
}

inline __scope_state __scope_after_join_start(__scope_state state) noexcept {
    switch (state) {
    case __scope_state::open:
    case __scope_state::open_and_joining:
        return __scope_state::open_and_joining;
    case __scope_state::closed:
    case __scope_state::closed_and_joining:
        return __scope_state::closed_and_joining;
    default:
        std::terminate();
    }
}

constexpr bool __scope_is_joining(__scope_state state) noexcept {
    return state == __scope_state::open_and_joining
        || state == __scope_state::closed_and_joining;
}

constexpr bool __scope_rejects_association(__scope_state state) noexcept {
    return state != __scope_state::unused
        && state != __scope_state::open
        && state != __scope_state::open_and_joining;
}

struct __association_result {
    __scope_state __state;
    std::size_t __count;
    bool __engaged;
};

constexpr __association_result __scope_try_associate(
    __scope_state state, std::size_t count, std::size_t limit) noexcept {
    if (count >= limit || __scope_rejects_association(state)) {
        return {state, count, false};
    }
    return {__scope_after_association(state), count + 1, true};
}

inline void __complete_joiners(__join_state_base* head) noexcept {
    while (head) {
        auto* current = head;
        head = head->__next;
        current->__next = nullptr;
        current->__registered = false;
        current->__complete(current);
    }
}

template<class Scope>
struct __join_sender;

} // namespace __forge_counting_scope

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

    auto get_env() const noexcept -> __empty_env {
        return {};
    }
};

} // namespace __forge_scope_detail

template<class Token>
concept scope_token =
    std::copyable<Token> &&
    requires(const Token token) {
        { token.try_associate() } -> scope_association;
        { token.wrap(std::declval<__forge_scope_detail::__test_sender>()) } -> sender_in<__empty_env>;
    };

// ──────────────────────────────────────────────────────────────────────────
// simple_counting_scope — structured concurrency scope subset
//
// wrap() follows the current working draft shape: simple scope tokens return
// their input sender unchanged. Association is owned by top-level algorithms
// such as associate/spawn/spawn_future. join() returns an async sender that
// completes when all currently associated work drains.
// ──────────────────────────────────────────────────────────────────────────

class simple_counting_scope {
public:
    class scope_token;
    class scope_association;
    using token = scope_token;

    static constexpr std::size_t max_associations =
        std::numeric_limits<std::size_t>::max();

    simple_counting_scope() noexcept = default;
    ~simple_counting_scope() noexcept {
        // The working draft requires joined, unused, or unused-and-closed.
        // Wait for the final decrement's critical section before teardown.
        using __state = __forge_counting_scope::__scope_state;
        __state state;
        {
            std::lock_guard lk{__mtx_};
            state = __state_.load(std::memory_order_relaxed);
        }
        // terminate() can invoke a user-installed handler; do not hold the lock.
        if (state != __state::joined && state != __state::unused
            && state != __state::unused_and_closed) {
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
        std::lock_guard lk{__mtx_};
        __state_.store(
            __forge_counting_scope::__scope_after_close(
                __state_.load(std::memory_order_relaxed)),
            std::memory_order_release);
    }

    [[nodiscard]] auto join() noexcept;

    [[nodiscard]] bool is_closed() const noexcept {
        return __forge_counting_scope::__scope_rejects_association(
            __state_.load(std::memory_order_acquire));
    }

    [[nodiscard]] std::size_t count() const noexcept {
        return __count_.load(std::memory_order_acquire);
    }

private:
    friend class scope_token;
    friend class scope_association;
    friend struct __forge_counting_scope::__join_sender<simple_counting_scope>;

    [[nodiscard]] scope_association __try_associate() noexcept;
    [[nodiscard]] bool __start_join(
        __forge_counting_scope::__join_state_base* joiner) noexcept;

    void __decrement() noexcept;

    std::atomic<std::size_t> __count_{0};
    std::atomic<__forge_counting_scope::__scope_state> __state_{
        __forge_counting_scope::__scope_state::unused};
    std::mutex __mtx_;
    __forge_counting_scope::__join_state_base* __joiners_ = nullptr;
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
    std::lock_guard lk{__mtx_};
    const auto next = __forge_counting_scope::__scope_try_associate(
        __state_.load(std::memory_order_relaxed),
        __count_.load(std::memory_order_relaxed), max_associations);
    if (!next.__engaged) return {};

    __state_.store(next.__state, std::memory_order_release);
    __count_.fetch_add(1, std::memory_order_relaxed);
    return scope_association{this};
}

inline bool simple_counting_scope::__start_join(
    __forge_counting_scope::__join_state_base* joiner) noexcept {
    {
        std::lock_guard lk{__mtx_};
        if (__count_.load(std::memory_order_acquire) == 0) {
            __state_.store(
                __forge_counting_scope::__scope_state::joined,
                std::memory_order_release);
            return true;
        } else {
            __state_.store(
                __forge_counting_scope::__scope_after_join_start(
                    __state_.load(std::memory_order_relaxed)),
                std::memory_order_release);
            joiner->__next = __joiners_;
            joiner->__registered = true;
            __joiners_ = joiner;
        }
    }
    return false;
}

inline void simple_counting_scope::__decrement() noexcept {
    __forge_counting_scope::__join_state_base* ready = nullptr;
    {
        std::lock_guard lk{__mtx_};
        if (__count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            auto state = __state_.load(std::memory_order_relaxed);
            if (__forge_counting_scope::__scope_is_joining(state)) {
                __state_.store(
                    __forge_counting_scope::__scope_state::joined,
                    std::memory_order_release);
                ready = std::exchange(__joiners_, nullptr);
            }
        }
    }
    __forge_counting_scope::__complete_joiners(ready);
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

    // wrap(sndr): current-WD identity wrapper. It does not associate work.
    template<sender S>
    [[nodiscard]] decltype(auto) wrap(S&& sndr) const noexcept;

private:
    simple_counting_scope* __scope_ = nullptr;
};

namespace __forge_counting_scope {

template<class S, class R>
struct __stop_op;

template<class R>
struct __join_scheduler_receiver {
    using receiver_concept = receiver_t;

    R* __rcvr;

    void set_value() && noexcept {
        std::execution::set_value(std::move(*__rcvr));
    }

    template<class E>
    void set_error(E&& error) && noexcept {
        std::execution::set_error(
            std::move(*__rcvr), static_cast<E&&>(error));
    }

    void set_stopped() && noexcept {
        std::execution::set_stopped(std::move(*__rcvr));
    }

    decltype(auto) get_env() const
        noexcept(noexcept(std::execution::get_env(*__rcvr))) {
        return std::execution::get_env(*__rcvr);
    }
};

template<class Env>
using __join_schedule_sender_t = decltype(std::execution::schedule(
    std::execution::get_start_scheduler(std::declval<Env>())));

template<class R>
using __join_schedule_op_t = connect_result_t<
    __join_schedule_sender_t<env_of_t<R>>,
    __join_scheduler_receiver<R>>;

template<class R>
concept __join_receiver =
    receiver<R> &&
    sender_to<
        __join_schedule_sender_t<env_of_t<R>>,
        __join_scheduler_receiver<R>>;

template<class Scope>
struct __join_sender {
    using sender_concept = sender_t;

    Scope* __scope;

    template<class Self, class Env>
    static constexpr auto get_completion_signatures() noexcept
        -> completion_signatures_of_t<__join_schedule_sender_t<Env>, Env> {
        return {};
    }

    auto get_env() const noexcept -> __empty_env {
        return {};
    }

    template<__join_receiver R>
    struct __op : __join_state_base, __forge_detail::__immovable {
        using operation_state_concept = operation_state_t;
        using __schedule_op_t = __join_schedule_op_t<R>;

        Scope* __scope;
        R __rcvr;
        __schedule_op_t __schedule_op;

        __op(Scope* scope, R rcvr)
            : __join_state_base(&__complete_join)
            , __scope(scope)
            , __rcvr(std::move(rcvr))
            , __schedule_op(std::execution::connect(
                  std::execution::schedule(std::execution::get_start_scheduler(
                      std::execution::get_env(__rcvr))),
                  __join_scheduler_receiver<R>{&__rcvr}))
        {}

        void start() & noexcept {
            auto* scope = __scope;
            if (scope && !scope->__start_join(this)) {
                return;
            }
            std::execution::set_value(std::move(__rcvr));
        }

        static void __complete_join(__join_state_base* base) noexcept {
            auto* self = static_cast<__op*>(base);
            std::execution::start(
                self->__schedule_op);
        }
    };

    template<__join_receiver R>
    auto connect(R rcvr) const -> __op<R> {
        return __op<R>{__scope, std::move(rcvr)};
    }
};

template<class Token>
struct __fused_stop_token {
    std::inplace_stop_token __input;
    Token __downstream;
    std::inplace_stop_token __fused;

    bool stop_requested() const noexcept {
        return __input.stop_requested() || __downstream.stop_requested();
    }
    bool stop_possible() const noexcept {
        return __input.stop_possible() || __downstream.stop_possible();
    }
    bool operator==(const __fused_stop_token&) const noexcept = default;

    template<class Fn>
    struct callback_type {
        struct __invoke {
            std::add_pointer_t<Fn> __fn;
            void operator()() const noexcept { std::forward<Fn>(*__fn)(); }
        };

        Fn __fn;
        std::inplace_stop_source __stopped_source;
        // Not named __callback: MSVC <sal.h> defines that identifier as a macro.
        std::inplace_stop_callback<__invoke> __registration;

        std::inplace_stop_token __callback_token(__fused_stop_token token) noexcept {
            // An input can be stopped while an earlier callback delays propagation.
            // Deliver this registration immediately without invoking other callbacks.
            if (token.stop_requested()) {
                __stopped_source.request_stop();
                return __stopped_source.get_token();
            }
            return token.__fused;
        }

        template<class Init>
            requires std::constructible_from<Fn, Init>
        explicit callback_type(__fused_stop_token token, Init&& init)
            noexcept(std::is_nothrow_constructible_v<Fn, Init> &&
                     std::is_nothrow_constructible_v<
                         std::inplace_stop_callback<__invoke>,
                         std::inplace_stop_token, __invoke>)
            : __fn(std::forward<Init>(init))
            // Initialize Fn before observing the inputs for registration.
            , __registration(__callback_token(token), __invoke{std::addressof(__fn)}) {}
    };
};

template<class BaseEnv, class Token = std::inplace_stop_token>
struct __stop_env {
    using base_t = std::remove_reference_t<BaseEnv>;

    // Own prvalues without moving them, and retain borrowed references.
    BaseEnv __base;
    Token __token;

    auto query(get_stop_token_t) const noexcept -> Token {
        return __token;
    }

    template<class Tag>
        requires (!std::same_as<std::remove_cvref_t<Tag>, get_stop_token_t> &&
                  __forge_env_detail::__forwarding_query<Tag> &&
                  __forge_env_detail::__queryable<Tag, const base_t&>)
    decltype(auto) query(Tag tag) const
        noexcept(__forge_env_detail::__nothrow_query<Tag, const base_t&>) {
        return __forge_env_detail::__query(
            std::move(tag), static_cast<const base_t&>(__base));
    }

};

template<class Env>
using __stop_env_t = __stop_env<Env>;

struct __request_fused_stop {
    std::inplace_stop_source* __source = nullptr;

    void operator()() const noexcept {
        if (__source) {
            __source->request_stop();
        }
    }
};

template<class Token, bool = std::stoppable_token_for<Token, __request_fused_stop>>
struct __optional_stop_callback {
    void install(Token token, std::inplace_stop_source& source) {
        if (token.stop_requested()) {
            source.request_stop();
        }
    }

    void reset() noexcept {}
};

template<class Token>
struct __optional_stop_callback<Token, true> {
    using callback_t = std::stop_callback_for_t<Token, __request_fused_stop>;

    void install(Token token, std::inplace_stop_source& source) {
        if (token.stop_requested()) {
            source.request_stop();
            return;
        }
        if (token.stop_possible()) {
            __cb.emplace(std::move(token), __request_fused_stop{&source});
        }
    }

    void reset() noexcept { __cb.reset(); }

    std::optional<callback_t> __cb;
};

} // namespace __forge_counting_scope

inline auto simple_counting_scope::join() noexcept {
    return __forge_counting_scope::__join_sender<simple_counting_scope>{this};
}

template<sender S>
[[nodiscard]] decltype(auto) simple_counting_scope::scope_token::wrap(S&& sndr) const noexcept {
    return std::forward<S>(sndr);
}

static_assert(std::execution::scope_token<simple_counting_scope::scope_token>);

inline simple_counting_scope::scope_token
simple_counting_scope::get_token() noexcept {
    return scope_token{this};
}

// ──────────────────────────────────────────────────────────────────────────
// counting_scope — stop-aware structured concurrency scope subset
// join() returns an async sender that completes when associated work drains.
// ──────────────────────────────────────────────────────────────────────────

class counting_scope {
public:
    class scope_token;
    class scope_association;
    using token = scope_token;

    static constexpr std::size_t max_associations =
        std::numeric_limits<std::size_t>::max();

    counting_scope() noexcept = default;
    ~counting_scope() noexcept {
        // The working draft requires joined, unused, or unused-and-closed.
        // Wait for the final decrement's critical section before teardown.
        using __state = __forge_counting_scope::__scope_state;
        __state state;
        {
            std::lock_guard lk{__mtx_};
            state = __state_.load(std::memory_order_relaxed);
        }
        // terminate() can invoke a user-installed handler; do not hold the lock.
        if (state != __state::joined && state != __state::unused
            && state != __state::unused_and_closed) {
            std::terminate();
        }
    }
    counting_scope(const counting_scope&) = delete;
    counting_scope& operator=(const counting_scope&) = delete;
    counting_scope(counting_scope&&) = delete;
    counting_scope& operator=(counting_scope&&) = delete;

    [[nodiscard]] scope_token get_token() noexcept;

    void close() noexcept {
        std::lock_guard lk{__mtx_};
        __state_.store(
            __forge_counting_scope::__scope_after_close(
                __state_.load(std::memory_order_relaxed)),
            std::memory_order_release);
    }

    void request_stop() noexcept {
        // The scope must remain alive through this call, even if a join completes.
        __stop_source_.request_stop();
    }

    [[nodiscard]] auto join() noexcept;

    [[nodiscard]] bool is_closed() const noexcept {
        return __forge_counting_scope::__scope_rejects_association(
            __state_.load(std::memory_order_acquire));
    }

    [[nodiscard]] std::size_t count() const noexcept {
        return __count_.load(std::memory_order_acquire);
    }

private:
    friend class scope_token;
    friend class scope_association;
    friend struct __forge_counting_scope::__join_sender<counting_scope>;

    [[nodiscard]] scope_association __try_associate() noexcept;
    [[nodiscard]] bool __start_join(
        __forge_counting_scope::__join_state_base* joiner) noexcept;

    [[nodiscard]] std::inplace_stop_token __stop_token() noexcept {
        return __stop_source_.get_token();
    }

    void __decrement() noexcept;

    std::atomic<std::size_t> __count_{0};
    std::atomic<__forge_counting_scope::__scope_state> __state_{
        __forge_counting_scope::__scope_state::unused};
    std::inplace_stop_source __stop_source_{};
    std::mutex __mtx_;
    __forge_counting_scope::__join_state_base* __joiners_ = nullptr;
};

class counting_scope::scope_association {
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
    friend class counting_scope;

    explicit scope_association(counting_scope* scope) noexcept
        : __scope_(scope) {}

    void __release() noexcept {
        if (!__scope_) return;
        auto* scope = std::exchange(__scope_, nullptr);
        scope->__decrement();
    }

    counting_scope* __scope_ = nullptr;
};

static_assert(std::execution::scope_association<counting_scope::scope_association>);

inline auto counting_scope::__try_associate() noexcept -> scope_association {
    std::lock_guard lk{__mtx_};
    const auto next = __forge_counting_scope::__scope_try_associate(
        __state_.load(std::memory_order_relaxed),
        __count_.load(std::memory_order_relaxed), max_associations);
    if (!next.__engaged) return {};

    __state_.store(next.__state, std::memory_order_release);
    __count_.fetch_add(1, std::memory_order_relaxed);
    return scope_association{this};
}

inline bool counting_scope::__start_join(
    __forge_counting_scope::__join_state_base* joiner) noexcept {
    {
        std::lock_guard lk{__mtx_};
        if (__count_.load(std::memory_order_acquire) == 0) {
            __state_.store(
                __forge_counting_scope::__scope_state::joined,
                std::memory_order_release);
            return true;
        } else {
            __state_.store(
                __forge_counting_scope::__scope_after_join_start(
                    __state_.load(std::memory_order_relaxed)),
                std::memory_order_release);
            joiner->__next = __joiners_;
            joiner->__registered = true;
            __joiners_ = joiner;
        }
    }
    return false;
}

inline void counting_scope::__decrement() noexcept {
    __forge_counting_scope::__join_state_base* ready = nullptr;
    {
        std::lock_guard lk{__mtx_};
        if (__count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            auto state = __state_.load(std::memory_order_relaxed);
            if (__forge_counting_scope::__scope_is_joining(state)) {
                __state_.store(
                    __forge_counting_scope::__scope_state::joined,
                    std::memory_order_release);
                ready = std::exchange(__joiners_, nullptr);
            }
        }
    }
    __forge_counting_scope::__complete_joiners(ready);
}

class counting_scope::scope_token {
public:
    scope_token() noexcept = default;
    explicit scope_token(counting_scope* scope) noexcept
        : __scope_(scope) {}

    [[nodiscard]] scope_association try_associate() const noexcept {
        if (!__scope_) return {};
        return __scope_->__try_associate();
    }

    template<sender S>
    [[nodiscard]] auto wrap(S&& sndr) const
        noexcept(std::is_nothrow_constructible_v<std::decay_t<S>, S>);

private:
    template<class S, class R>
    friend struct __forge_counting_scope::__stop_op;

    [[nodiscard]] std::inplace_stop_token __stop_token() const noexcept {
        if (!__scope_) return {};
        return __scope_->__stop_token();
    }

    counting_scope* __scope_ = nullptr;
};

namespace __forge_counting_scope {

template<class S, class R>
struct __stop_op : __forge_detail::__immovable {
    using operation_state_concept = operation_state_t;

    using token_t = std::inplace_stop_token;
    using downstream_env_t = env_of_t<R>;
    using downstream_stop_token_t = std::remove_cvref_t<decltype(
        std::execution::get_stop_token(std::declval<downstream_env_t>()))>;
    using child_token_t = std::conditional_t<
        std::unstoppable_token<downstream_stop_token_t>, token_t,
        __fused_stop_token<downstream_stop_token_t>>;

    struct __recv {
        using receiver_concept = receiver_t;

        __stop_op* __owner;
        child_token_t __token;

        template<class... Vs>
        void set_value(Vs&&... vs) && noexcept {
            auto* rcvr = &__owner->__rcvr;
            __owner->__release_stop_callbacks();
            std::execution::set_value(std::move(*rcvr), static_cast<Vs&&>(vs)...);
        }

        template<class E>
        void set_error(E&& e) && noexcept {
            auto* rcvr = &__owner->__rcvr;
            __owner->__release_stop_callbacks();
            std::execution::set_error(std::move(*rcvr), static_cast<E&&>(e));
        }

        void set_stopped() && noexcept {
            auto* rcvr = &__owner->__rcvr;
            __owner->__release_stop_callbacks();
            std::execution::set_stopped(std::move(*rcvr));
        }

        auto get_env() const noexcept -> __stop_env<env_of_t<R>, child_token_t> {
            return __stop_env<env_of_t<R>, child_token_t>{
                std::execution::get_env(__owner->__rcvr),
                __token};
        }
    };

    using inner_op_t = connect_result_t<S, __recv>;

    token_t __token;
    R __rcvr;
    std::inplace_stop_source __fused_source;
    __optional_stop_callback<std::inplace_stop_token> __scope_stop;
    __optional_stop_callback<downstream_stop_token_t> __downstream_stop;
    bool __registration_failed = false;
    inner_op_t __inner_op;

    __stop_op(token_t token, S sndr, R rcvr)
        : __token(token)
        , __rcvr(std::move(rcvr))
        , __inner_op(std::execution::connect(
              std::move(sndr), __recv{this, __prepare_token()}))
    {}

    child_token_t __prepare_token() {
        if constexpr (!std::unstoppable_token<downstream_stop_token_t>) {
            auto downstream = std::execution::get_stop_token(std::execution::get_env(__rcvr));
            try {
                __scope_stop.install(__token, __fused_source);
                __downstream_stop.install(downstream, __fused_source);
            } catch (...) {
                __release_stop_callbacks();
                __registration_failed = true;
                __fused_source.request_stop();
            }
            return {__token, std::move(downstream), __fused_source.get_token()};
        } else {
            return __token;
        }
    }

    void start() & noexcept {
        if (__registration_failed) {
            std::execution::set_stopped(std::move(__rcvr));
            return;
        }
        std::execution::start(__inner_op);
    }

    // Deregistration waits for input callbacks executing on other threads.
    // Forward unchanged only after their fused-source requests have returned.
    // Same-thread source destruction still relies on the backport control block.
    void __release_stop_callbacks() noexcept {
        __scope_stop.reset();
        __downstream_stop.reset();
    }
};

template<class S>
struct __stop_sender {
    using sender_concept = sender_t;
    using source_t = S;

    std::inplace_stop_token __token;
    S __sndr;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept {
        using self_t = std::remove_cvref_t<Self>;
        using downstream_token_t = std::remove_cvref_t<decltype(
            std::execution::get_stop_token(std::declval<Env>()))>;
        using child_token_t = std::conditional_t<
            std::unstoppable_token<downstream_token_t>, std::inplace_stop_token,
            __fused_stop_token<downstream_token_t>>;
        using child_env_t = __stop_env<Env, child_token_t>;
        using up_cs_t = completion_signatures_of_t<
            typename self_t::source_t&&,
            child_env_t>;
        return __forge_meta::__concat_unique_cs_t<
            up_cs_t,
            completion_signatures<set_stopped_t()>>{};
    }

    template<receiver R>
    auto connect(R r) &&
        -> __stop_op<S, R>
    {
        return __stop_op<S, R>{__token, std::move(__sndr), std::move(r)};
    }

    template<receiver R>
        requires std::copy_constructible<S>
    auto connect(R r) const&
        -> __stop_op<S, R>
    {
        return __stop_op<S, R>{__token, __sndr, std::move(r)};
    }

    auto get_env() const noexcept -> env_of_t<S> {
        return std::execution::get_env(__sndr);
    }
};

} // namespace __forge_counting_scope

template<sender S>
[[nodiscard]] auto counting_scope::scope_token::wrap(S&& sndr) const
    noexcept(std::is_nothrow_constructible_v<std::decay_t<S>, S>) {
    return __forge_counting_scope::__stop_sender<std::decay_t<S>>{
        __stop_token(),
        std::forward<S>(sndr)};
}

static_assert(std::execution::scope_token<counting_scope::scope_token>);

inline counting_scope::scope_token
counting_scope::get_token() noexcept {
    return scope_token{this};
}

inline auto counting_scope::join() noexcept {
    return __forge_counting_scope::__join_sender<counting_scope>{this};
}

} // namespace std::execution
