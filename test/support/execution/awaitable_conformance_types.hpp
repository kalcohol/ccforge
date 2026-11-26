#pragma once
#include <coroutine>
#include <exception>
#include <execution>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace awaitable_tests {
namespace ex = std::execution;

struct int_awaiter {
    bool await_ready() const noexcept { return true; }
    void await_suspend(std::coroutine_handle<>) const noexcept {}
    int await_resume() const noexcept { return 17; }
};
struct void_awaiter {
    bool await_ready() const noexcept { return true; }
    void await_suspend(std::coroutine_handle<>) const noexcept {}
    void await_resume() const noexcept {}
};
struct cv_void_awaiter : void_awaiter {
    using result_t = const void;
    auto await_resume() const noexcept -> result_t {}
};
struct ref_awaiter {
    int* value;
    bool await_ready() const noexcept { return true; }
    void await_suspend(std::coroutine_handle<>) const noexcept {}
    int& await_resume() const noexcept { return *value; }
};
struct member_awaitable {
    auto operator co_await() && noexcept -> int_awaiter { return {}; }
};
struct free_awaitable {};
inline auto operator co_await(free_awaitable) noexcept -> int_awaiter { return {}; }
struct ambiguous_awaitable {
    auto operator co_await() noexcept -> int_awaiter { return {}; }
};
inline auto operator co_await(ambiguous_awaitable&) noexcept -> int_awaiter { return {}; }
inline auto operator co_await(ambiguous_awaitable&&) noexcept -> int_awaiter { return {}; }
struct member_adapted {
    template<class P>
    auto as_awaitable(P&) noexcept -> int_awaiter { return {}; }
};
struct disabled_awaitable : int_awaiter {};
struct address_awaiter : int_awaiter {
    auto operator&() -> address_awaiter* = delete;
};
struct not_awaitable {};
struct bad_suspend : int_awaiter {
    int await_suspend(std::coroutine_handle<>) const noexcept { return 0; }
};

// Both customization constraints deliberately ask sender<Self>. The marker
// and member-signature paths must stop before probing either customization.
struct recursive_marked {
    using sender_concept = ex::sender_tag;
    template<class Self, class Env>
    static constexpr auto get_completion_signatures() noexcept
        -> ex::completion_signatures<ex::set_value_t(int), ex::set_error_t(std::exception_ptr),
            ex::set_stopped_t(), ex::set_error_t(long)> { return {}; }
    template<class Self = recursive_marked> requires ex::sender<Self>
    auto operator co_await() && noexcept -> int_awaiter { return {}; }
    template<class P, class Self = recursive_marked> requires ex::sender<Self>
    auto as_awaitable(P&) noexcept -> int_awaiter { return {}; }
};

struct promise {
    auto get_env() const noexcept -> ex::env<> { return {}; }
    auto operator&() -> promise* = delete;
};
struct mixin_promise : ex::with_awaitable_senders<mixin_promise> {};
struct explicit_stopped_result {
    explicit operator std::coroutine_handle<>() const noexcept { return std::noop_coroutine(); }
};
struct explicit_stopped_promise : promise {
    auto unhandled_stopped() noexcept -> explicit_stopped_result { return {}; }
};
struct rejecting_promise {
    void await_transform(int) = delete;
};

struct receiver {
    using receiver_concept = ex::receiver_tag;
    template<class... Ts> void set_value(Ts&&...) && noexcept {}
    void set_error(std::exception_ptr) && noexcept {}
    void set_stopped() && noexcept {}
    auto operator&() -> receiver* = delete;
};

// This awaiter is a sender in the default environment, but cannot be awaited
// with a stoppable token. It tests environment-sensitive Forge constraints.
struct unstoppable_awaiter : int_awaiter {
    template<class P>
        requires std::unstoppable_token<std::stop_token_of_t<ex::env_of_t<P>>>
    void await_suspend(std::coroutine_handle<P>) const noexcept {}
};

struct raw_sender;
struct await_domain {
    template<class Env>
    auto transform_sender(ex::set_value_t, raw_sender&&, const Env&) const noexcept -> int_awaiter;
};
struct raw_env {
    template<class Env>
    auto query(ex::get_completion_domain_t<ex::set_value_t>, const Env&) const noexcept
        -> await_domain { return {}; }
};
struct raw_sender {
    using sender_concept = ex::sender_tag;
    auto get_env() const noexcept -> raw_env { return {}; }
};
template<class Env>
auto await_domain::transform_sender(ex::set_value_t, raw_sender&&, const Env&) const noexcept -> int_awaiter {
    return {};
}

struct start_sender {
    using sender_concept = ex::sender_tag;
    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> ex::completion_signatures<ex::set_value_t(int), ex::set_error_t(std::exception_ptr),
            ex::set_stopped_t()> { return {}; }
};
struct start_domain {
    template<class Env>
    auto transform_sender(ex::start_t, start_sender&&, const Env&) const noexcept
        -> int_awaiter { return {}; }
};
struct start_env {
    auto query(ex::get_domain_t) const noexcept -> start_domain { return {}; }
};
struct start_receiver : receiver {
    auto get_env() const noexcept -> start_env { return {}; }
};

// The erasure's copyable-source policy borrows this sender as an lvalue.
struct lvalue_sender {
    using sender_concept = ex::sender_tag;
    template<class Self, class Env> requires std::is_lvalue_reference_v<Self>
    static auto get_completion_signatures() noexcept
        -> ex::completion_signatures<ex::set_value_t(int)> { return {}; }
    template<class R>
    struct operation {
        using operation_state_concept = ex::operation_state_tag;
        R receiver;
        void start() & noexcept { ex::set_value(std::move(receiver), 29); }
    };
    template<ex::receiver R>
    auto connect(R r) & -> operation<R> { return {std::move(r)}; }
};

// The prepared sender lives outside the environment and the adaptor.
template<class S, bool Consume = false>
struct bridge_adaptor_source {
    using sender_concept = ex::sender_tag;
    S* prepared;

    template<class Self, class Env>
    static constexpr auto get_completion_signatures() noexcept
        -> ex::completion_signatures<ex::set_value_t(int)> { return {}; }

    auto get_env() const noexcept {
        auto adaptor = [target = prepared](auto&&) -> decltype(auto) {
            if constexpr (Consume) {
                return S(std::move(*target));
            } else {
                return *target;
            }
        };
        return ex::prop(ex::get_await_completion_adaptor, adaptor);
    }
};

// Vary every expression in 7.2 independently, while keeping explicit types.
template<bool N> struct adapted_member {
    template<class P>
    auto as_awaitable(P&) noexcept(N) -> int_awaiter { return {}; }
};
template<bool Adapt, bool Member> struct adaptor {
    template<class S>
    auto operator()(S&&) const noexcept(Adapt) -> adapted_member<Member> { return {}; }
};
template<bool Adapt, bool Member> struct adaptor_env {
    auto query(ex::get_await_completion_adaptor_t) const noexcept -> adaptor<Adapt, Member> { return {}; }
};
template<bool Adapt, bool Member> struct adapted_sender {
    using sender_concept = ex::sender_tag;
    template<class Self, class Env>
    static constexpr auto get_completion_signatures() noexcept
        -> ex::completion_signatures<ex::set_value_t(int)> { return {}; }
    auto get_env() const noexcept -> adaptor_env<Adapt, Member> { return {}; }
};
struct throwing_source { using sender_concept = ex::sender_tag; };
struct throwing_domain {
    template<class Tag, class Env>
    auto transform_sender(Tag, throwing_source&&, const Env&) const noexcept(false)
        -> adapted_sender<true, true> { return {}; }
};
struct throwing_domain_env {
    auto query(ex::get_domain_t) const noexcept -> throwing_domain { return {}; }
};
struct throwing_transform_promise {
    auto get_env() const noexcept -> throwing_domain_env { return {}; }
};
} // namespace awaitable_tests

template<>
inline constexpr bool std::execution::enable_sender<awaitable_tests::disabled_awaitable> = false;

namespace awaitable_adl {
struct value { int number; };
struct result {
    int completion = 0;
    int number = 0;
    std::exception_ptr error;
};
struct receiver {
    using receiver_concept = std::execution::receiver_tag;
    result* output;
    void set_value(value v) && noexcept {
        output->number = v.number;
        output->completion = 1;
    }
    void set_error(std::exception_ptr error) && noexcept {
        output->error = std::move(error);
        output->completion = 2;
    }
    void set_stopped() && noexcept { output->completion = 3; }
};
// Neither the receiver nor value namespace may participate in the bridge's
// internal completion await. Probing this overload there would recurse.
template<std::execution::sender S>
auto operator co_await(S&&) noexcept -> awaitable_tests::int_awaiter { return {}; }
} // namespace awaitable_adl

namespace awaitable_tests {
// This non-template awaiter has no ADL association with its result namespace.
struct adl_value_awaiter {
    bool fail = false;
    bool await_ready() const noexcept { return true; }
    void await_suspend(std::coroutine_handle<>) const noexcept {}
    auto await_resume() const -> awaitable_adl::value {
        if (fail) throw std::runtime_error("ADL result");
        return {61};
    }
};
} // namespace awaitable_tests
