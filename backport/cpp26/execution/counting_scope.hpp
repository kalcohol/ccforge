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

#include <atomic>
#include <cstddef>
#include <exception>
#include <mutex>
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

    simple_counting_scope() noexcept = default;
    ~simple_counting_scope() noexcept {
        // Destructor: if count > 0, work was not properly joined.
        // Forge follows the standard-style precondition here and terminates.
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
        std::lock_guard lk{__mtx_};
        __closed_.store(true, std::memory_order_release);
    }

    [[nodiscard]] auto join() noexcept;

    [[nodiscard]] bool is_closed() const noexcept {
        return __closed_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::size_t count() const noexcept {
        return __count_.load(std::memory_order_acquire);
    }

private:
    friend class scope_token;
    friend class scope_association;
    friend struct __forge_counting_scope::__join_sender<simple_counting_scope>;

    [[nodiscard]] scope_association __try_associate() noexcept;
    void __start_join(__forge_counting_scope::__join_state_base* joiner) noexcept;

    void __decrement() noexcept;

    std::atomic<std::size_t> __count_{0};
    std::atomic<bool> __closed_{false};
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
    if (__closed_.load(std::memory_order_acquire)) return {};
    __count_.fetch_add(1, std::memory_order_relaxed);
    return scope_association{this};
}

inline void simple_counting_scope::__start_join(
    __forge_counting_scope::__join_state_base* joiner) noexcept {
    __forge_counting_scope::__join_state_base* ready = nullptr;
    {
        std::lock_guard lk{__mtx_};
        if (__count_.load(std::memory_order_acquire) == 0) {
            ready = joiner;
        } else {
            joiner->__next = __joiners_;
            joiner->__registered = true;
            __joiners_ = joiner;

            if (__count_.load(std::memory_order_acquire) == 0) {
                __joiners_ = joiner->__next;
                ready = joiner;
            }
        }
    }
    __forge_counting_scope::__complete_joiners(ready);
}

inline void simple_counting_scope::__decrement() noexcept {
    __forge_counting_scope::__join_state_base* ready = nullptr;
    {
        std::lock_guard lk{__mtx_};
        if (__count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            ready = std::exchange(__joiners_, nullptr);
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

template<class Scope>
struct __join_sender {
    using sender_concept = sender_t;

    Scope* __scope;

    template<class Self, class Env>
    static constexpr auto get_completion_signatures() noexcept
        -> completion_signatures<set_value_t()> {
        return {};
    }

    auto get_env() const noexcept -> empty_env {
        return {};
    }

    template<class R>
    struct __op : __join_state_base, __forge_detail::__immovable {
        using operation_state_concept = operation_state_t;

        Scope* __scope;
        R __rcvr;

        __op(Scope* scope, R rcvr) noexcept(std::is_nothrow_move_constructible_v<R>)
            : __join_state_base(&__complete_join)
            , __scope(scope)
            , __rcvr(std::move(rcvr)) {}

        void start() & noexcept {
            if (__scope) {
                __scope->__start_join(this);
                return;
            }
            __complete_join(this);
        }

        static void __complete_join(__join_state_base* base) noexcept {
            auto* self = static_cast<__op*>(base);
            std::execution::set_value(std::move(self->__rcvr));
        }
    };

    template<receiver R>
    auto connect(R rcvr) const -> __op<R> {
        return __op<R>{__scope, std::move(rcvr)};
    }
};

template<class BaseEnv>
struct __stop_env {
    [[no_unique_address]] BaseEnv __base;
    std::inplace_stop_token __token;

    friend auto tag_invoke(get_stop_token_t, const __stop_env& self) noexcept
        -> std::inplace_stop_token {
        return self.__token;
    }

    template<class Tag>
        requires (!std::same_as<std::remove_cvref_t<Tag>, get_stop_token_t> &&
                  __forge_detail::tag_invocable<Tag, const BaseEnv&>)
    friend decltype(auto) tag_invoke(Tag tag, const __stop_env& self)
        noexcept(__forge_detail::nothrow_tag_invocable<Tag, const BaseEnv&>) {
        return __forge_detail::tag_invoke_fn(tag, self.__base);
    }
};

template<class Env>
using __stop_env_t = __stop_env<std::decay_t<Env>>;

template<class CS>
struct __declares_exception_error : std::false_type {};

template<class... Sigs>
struct __declares_exception_error<completion_signatures<Sigs...>>
    : std::bool_constant<
          __forge_meta::list_contains_v<
              __forge_meta::type_list<Sigs...>,
              set_error_t(std::exception_ptr)>> {};

template<class S, class R, class = void>
struct __can_report_connect_exception : std::false_type {};

template<class S, class R>
struct __can_report_connect_exception<
    S,
    R,
    std::void_t<completion_signatures_of_t<
        const S&,
        __stop_env_t<env_of_t<R>>>>>
    : __declares_exception_error<
          completion_signatures_of_t<
              const S&,
              __stop_env_t<env_of_t<R>>>> {};

template<class S, class R>
inline constexpr bool __can_report_connect_exception_v =
    __can_report_connect_exception<S, R>::value;

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

    counting_scope() noexcept = default;
    ~counting_scope() noexcept {
        if (__count_.load(std::memory_order_acquire) != 0) {
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
        __closed_.store(true, std::memory_order_release);
    }

    bool request_stop() noexcept {
        return __stop_source_.request_stop();
    }

    [[nodiscard]] auto join() noexcept;

    [[nodiscard]] bool is_closed() const noexcept {
        return __closed_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::size_t count() const noexcept {
        return __count_.load(std::memory_order_acquire);
    }

private:
    friend class scope_token;
    friend class scope_association;
    friend struct __forge_counting_scope::__join_sender<counting_scope>;

    [[nodiscard]] scope_association __try_associate() noexcept;
    void __start_join(__forge_counting_scope::__join_state_base* joiner) noexcept;

    [[nodiscard]] std::inplace_stop_token __stop_token() noexcept {
        return __stop_source_.get_token();
    }

    void __decrement() noexcept;

    std::atomic<std::size_t> __count_{0};
    std::atomic<bool> __closed_{false};
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
    if (__closed_.load(std::memory_order_acquire)) return {};
    __count_.fetch_add(1, std::memory_order_relaxed);
    return scope_association{this};
}

inline void counting_scope::__start_join(
    __forge_counting_scope::__join_state_base* joiner) noexcept {
    __forge_counting_scope::__join_state_base* ready = nullptr;
    {
        std::lock_guard lk{__mtx_};
        if (__count_.load(std::memory_order_acquire) == 0) {
            ready = joiner;
        } else {
            joiner->__next = __joiners_;
            joiner->__registered = true;
            __joiners_ = joiner;

            if (__count_.load(std::memory_order_acquire) == 0) {
                __joiners_ = joiner->__next;
                ready = joiner;
            }
        }
    }
    __forge_counting_scope::__complete_joiners(ready);
}

inline void counting_scope::__decrement() noexcept {
    __forge_counting_scope::__join_state_base* ready = nullptr;
    {
        std::lock_guard lk{__mtx_};
        if (__count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            ready = std::exchange(__joiners_, nullptr);
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
    [[nodiscard]] auto wrap(S&& sndr) const;

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

    using token_t = counting_scope::scope_token;

    struct __recv {
        using receiver_concept = receiver_t;

        R* __rcvr;
        std::inplace_stop_token __token;

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

        auto get_env() const noexcept {
            return __stop_env_t<env_of_t<R>>{
                std::execution::get_env(*__rcvr),
                __token};
        }
    };

    using inner_op_t = connect_result_t<S, __recv>;

    token_t __token;
    S __sndr;
    R __rcvr;
    __forge_detail::__op_storage<1024> __inner_storage;

    __stop_op(token_t token, S sndr, R rcvr)
        : __token(token)
        , __sndr(std::move(sndr))
        , __rcvr(std::move(rcvr))
    {}

    ~__stop_op() {
        __inner_storage.destroy();
    }

    void start() & noexcept {
        try {
            auto stop_token = __token.__stop_token();
            auto* op = __inner_storage.template emplace_from<inner_op_t>([&]() -> inner_op_t {
                return std::execution::connect(
                    std::move(__sndr),
                    __recv{&__rcvr, stop_token});
            });
            std::execution::start(*op);
        } catch (...) {
            if constexpr (__can_report_connect_exception_v<S, R>) {
                std::execution::set_error(std::move(__rcvr), std::current_exception());
            } else {
                std::execution::set_stopped(std::move(__rcvr));
            }
        }
    }
};

template<class S>
struct __stop_sender {
    using sender_concept = sender_t;
    using source_t = S;

    counting_scope::scope_token __token;
    S __sndr;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept {
        using self_t = std::remove_cvref_t<Self>;
        using child_env_t = __stop_env_t<Env>;
        using up_cs_t = decltype(std::execution::get_completion_signatures(
            std::declval<const typename self_t::source_t&>(),
            std::declval<child_env_t>()));
        return up_cs_t{};
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
[[nodiscard]] auto counting_scope::scope_token::wrap(S&& sndr) const {
    return __forge_counting_scope::__stop_sender<std::decay_t<S>>{
        *this,
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
