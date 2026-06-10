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

#include "any_stop_token.hpp"
#include "concepts.hpp"
#include "counting_scope.hpp"
#include "detail/op_storage.hpp"
#include "env.hpp"
#include "write_env.hpp"

#include <atomic>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace std::execution {

namespace __forge_spawn_future {

template<class Tuple>
struct __value_result {
    Tuple __values;
};

template<class Error>
struct __error_result {
    Error __error;
};

struct __stopped_result {};

template<class Sig>
struct __result_alt;

template<class... Vs>
struct __result_alt<set_value_t(Vs...)> {
    using type = __value_result<std::tuple<std::decay_t<Vs>...>>;
};

template<class Error>
struct __result_alt<set_error_t(Error)> {
    using type = __error_result<std::decay_t<Error>>;
};

template<>
struct __result_alt<set_stopped_t()> {
    using type = __stopped_result;
};

template<class Sig>
using __result_alt_t = typename __result_alt<Sig>::type;

template<class CS>
struct __result_variant;

template<class... Sigs>
struct __result_variant<completion_signatures<Sigs...>> {
private:
    using completion_list = __forge_meta::list_push_unique_all_t<
        __forge_meta::type_list<std::monostate>,
        __result_alt_t<Sigs>...>;
    using with_exception = __forge_meta::list_push_unique_t<
        completion_list,
        __error_result<std::exception_ptr>>;

public:
    using type = __forge_meta::list_to_variant_t<std::variant, with_exception>;
};

template<class CS>
using __result_variant_t = typename __result_variant<CS>::type;

template<class CS>
using __future_completion_signatures_t = __forge_meta::__concat_unique_cs_t<
    CS,
    completion_signatures<set_error_t(std::exception_ptr), set_stopped_t()>>;

template<class Env>
struct __spawn_env {
    [[no_unique_address]] Env __env;
    std::inplace_stop_source* __source;

    friend auto tag_invoke(get_stop_token_t, const __spawn_env& self) noexcept {
        return self.__source->get_token();
    }

    template<class Tag>
        requires (!std::same_as<std::remove_cvref_t<Tag>, get_stop_token_t> &&
                  __forge_detail::tag_invocable<Tag, const Env&>)
    friend decltype(auto) tag_invoke(Tag tag, const __spawn_env& self)
        noexcept(__forge_detail::nothrow_tag_invocable<Tag, const Env&>) {
        return __forge_detail::tag_invoke_fn(tag, self.__env);
    }
};

template<class State>
struct __inner_recv;

template<class State>
struct __consumer_base;

template<class Env>
struct __allocator_for_env {
    using type = std::allocator<std::byte>;

    static type get(Env&) noexcept {
        return {};
    }
};

template<class Env>
    requires requires(Env& env) { std::execution::get_allocator(env); }
struct __allocator_for_env<Env> {
    using type = std::decay_t<decltype(std::execution::get_allocator(std::declval<Env&>()))>;

    static type get(Env& env) {
        return std::execution::get_allocator(env);
    }
};

template<class Env>
using __allocator_for_env_t = typename __allocator_for_env<Env>::type;

template<class S, class Env, class Association>
struct __shared_state : std::enable_shared_from_this<__shared_state<S, Env, Association>> {
    using source_t = S;
    using env_t = Env;
    using association_t = Association;
    using spawn_env_t = __spawn_env<Env>;
    using started_sender_t = decltype(std::execution::write_env(
        std::declval<S>(), std::declval<spawn_env_t>()));
    using upstream_cs_t = decltype(std::execution::get_completion_signatures(
        std::declval<started_sender_t>(), std::declval<empty_env>()));
    using output_cs_t = __future_completion_signatures_t<upstream_cs_t>;
    using result_t = __result_variant_t<output_cs_t>;
    using inner_recv_t = __inner_recv<__shared_state>;
    using inner_op_t = connect_result_t<started_sender_t, inner_recv_t>;
    using consumer_base_t = __consumer_base<__shared_state>;
    using allocator_t = __allocator_for_env_t<Env>;

    enum class __phase_t { running, done };
    enum class __consume_result { registered, deliver_now, already_consumed };

    explicit __shared_state(Association assoc, allocator_t allocator) noexcept
        : __association(std::move(assoc))
        , __state_allocator(std::move(allocator))
    {}

    template<class T, class... Args>
    [[nodiscard]] auto __allocate_shared_aux(Args&&... args) {
        using alloc_t = typename std::allocator_traits<allocator_t>
            ::template rebind_alloc<T>;
        return std::allocate_shared<T>(
            alloc_t{__state_allocator}, std::forward<Args>(args)...);
    }

    ~__shared_state() noexcept {
        __op_storage.destroy();
        __association = {};
    }

    [[nodiscard]] bool __has_association() const noexcept {
        return static_cast<bool>(__association);
    }

    void __start(S sndr, Env env) noexcept {
        __keepalive = this->shared_from_this();
        try {
            spawn_env_t spawn_env{std::move(env), &__stop_source};
            auto started = std::execution::write_env(std::move(sndr), std::move(spawn_env));
            auto weak = this->weak_from_this();
            auto* op = __op_storage.template emplace_from<inner_op_t>([&]() -> inner_op_t {
                return std::execution::connect(std::move(started), inner_recv_t{weak});
            });
            std::execution::start(*op);
        } catch (...) {
            __complete_with(__error_result<std::exception_ptr>{std::current_exception()});
        }
    }

    void __complete_not_started() noexcept {
        __complete_with(__stopped_result{});
    }

    template<class Alt>
    void __complete_with(Alt alt) noexcept {
        std::weak_ptr<consumer_base_t> consumer_cb;
        Association association;
        {
            std::lock_guard lk{__mtx};
            if (__phase == __phase_t::done) {
                return;
            }
            try {
                __result.template emplace<std::decay_t<Alt>>(std::move(alt));
            } catch (...) {
                __result.template emplace<__error_result<std::exception_ptr>>(
                std::current_exception());
            }
            __phase = __phase_t::done;
            association = std::move(__association);
            consumer_cb = std::move(__consumer_cb);
        }
        association = {};

        if (auto consumer = consumer_cb.lock()) {
            consumer->__deliver(*this);
        }

        std::shared_ptr<__shared_state> keepalive;
        {
            std::lock_guard lk{__mtx};
            keepalive = std::move(__keepalive);
        }
    }

    void __request_stop() noexcept {
        __stop_source.request_stop();
    }

    void __abandon_unconsumed() noexcept {
        bool should_cancel = false;
        {
            std::lock_guard lk{__mtx};
            if (!__consumer_taken) {
                __consumer_taken = true;
                should_cancel = (__phase != __phase_t::done);
            }
        }
        if (should_cancel) {
            __request_stop();
        }
    }

    __consume_result __consume(std::weak_ptr<consumer_base_t> cb) {
        std::lock_guard lk{__mtx};
        if (__consumer_taken) {
            return __consume_result::already_consumed;
        }
        __consumer_taken = true;
        if (__phase == __phase_t::done) {
            return __consume_result::deliver_now;
        }
        __consumer_cb = std::move(cb);
        return __consume_result::registered;
    }

    template<class R>
    void __deliver_to(R& rcvr) noexcept {
        std::visit([&](auto& result) noexcept {
            using result_type = std::decay_t<decltype(result)>;
            if constexpr (std::is_same_v<result_type, std::monostate>) {
                std::execution::set_stopped(std::move(rcvr));
            } else if constexpr (std::is_same_v<result_type, __stopped_result>) {
                std::execution::set_stopped(std::move(rcvr));
            } else if constexpr (requires { result.__values; }) {
                std::apply([&](auto&... vs) noexcept {
                    std::execution::set_value(std::move(rcvr), std::move(vs)...);
                }, result.__values);
            } else {
                std::execution::set_error(std::move(rcvr), std::move(result.__error));
            }
        }, __result);
    }

    std::mutex __mtx{};
    __phase_t __phase = __phase_t::running;
    bool __consumer_taken = false;
    result_t __result{};
    std::weak_ptr<consumer_base_t> __consumer_cb{};
    std::inplace_stop_source __stop_source{};
    Association __association{};
    allocator_t __state_allocator;
    __forge_detail::__op_storage<1024> __op_storage{};
    std::shared_ptr<__shared_state> __keepalive{};
};

template<class State>
struct __inner_recv {
    using receiver_concept = receiver_t;

    std::weak_ptr<State> __state;

    template<class... Vs>
    void set_value(Vs&&... vs) && noexcept {
        auto state = __state.lock();
        if (!state) {
            return;
        }
        using tuple_t = std::tuple<std::decay_t<Vs>...>;
        using alt_t = __value_result<tuple_t>;
        try {
            state->__complete_with(alt_t{tuple_t{static_cast<Vs&&>(vs)...}});
        } catch (...) {
            state->__complete_with(__error_result<std::exception_ptr>{std::current_exception()});
        }
    }

    template<class Error>
    void set_error(Error&& error) && noexcept {
        auto state = __state.lock();
        if (!state) {
            return;
        }
        using alt_t = __error_result<std::decay_t<Error>>;
        try {
            state->__complete_with(alt_t{static_cast<Error&&>(error)});
        } catch (...) {
            state->__complete_with(__error_result<std::exception_ptr>{std::current_exception()});
        }
    }

    void set_stopped() && noexcept {
        auto state = __state.lock();
        if (!state) {
            return;
        }
        state->__complete_with(__stopped_result{});
    }

    auto get_env() const noexcept -> empty_env {
        return {};
    }
};

template<class State>
struct __consumer_base {
    virtual ~__consumer_base() = default;
    virtual void __deliver(State& state) noexcept = 0;
};

template<class State, class R>
struct __consumer : __consumer_base<State> {
    struct __stop_callback_fn {
        std::weak_ptr<State> __state;

        void operator()() noexcept {
            if (auto state = __state.lock()) {
                state->__request_stop();
            }
        }
    };

    using callback_t = std::stop_callback_for_t<std::any_stop_token, __stop_callback_fn>;

    explicit __consumer(R rcvr)
        : __rcvr(std::move(rcvr))
    {}

    void __install_stop_callback(const std::shared_ptr<State>& state) noexcept {
        __state = state;
        try {
            auto token = std::any_stop_token{
                std::execution::get_stop_token(std::execution::get_env(__rcvr))};
            if (token.stop_requested()) {
                state->__request_stop();
            }
            if (token.stop_possible()) {
                __stop_callback.emplace(token, __stop_callback_fn{state});
            }
        } catch (...) {
            state->__request_stop();
        }
    }

    void __deliver(State& state) noexcept override {
        if (__active.exchange(false, std::memory_order_acq_rel)) {
            __stop_callback.reset();
            state.__deliver_to(__rcvr);
        }
    }

    void __abandon(State& state) noexcept {
        if (__active.exchange(false, std::memory_order_acq_rel)) {
            __stop_callback.reset();
            state.__request_stop();
        }
    }

    void __deliver_stopped() noexcept {
        if (__active.exchange(false, std::memory_order_acq_rel)) {
            __stop_callback.reset();
            std::execution::set_stopped(std::move(__rcvr));
        }
    }

    void __deliver_error(std::exception_ptr error) noexcept {
        if (__active.exchange(false, std::memory_order_acq_rel)) {
            __stop_callback.reset();
            std::execution::set_error(std::move(__rcvr), std::move(error));
        }
    }

    R __rcvr;
    std::weak_ptr<State> __state{};
    std::optional<callback_t> __stop_callback{};
    std::atomic<bool> __active{true};
};

template<class State, class R>
struct __op : __forge_detail::__immovable {
    using operation_state_concept = operation_state_t;
    using consumer_t = __consumer<State, R>;

    std::shared_ptr<State> __state;
    std::shared_ptr<consumer_t> __consumer_state;

    __op(std::shared_ptr<State> state, R rcvr)
        : __state(std::move(state))
        , __consumer_state(__state->template __allocate_shared_aux<consumer_t>(std::move(rcvr)))
    {}

    ~__op() noexcept {
        if (__state && __consumer_state) {
            __consumer_state->__abandon(*__state);
        }
    }

    void start() & noexcept {
        auto state = __state;
        auto consumer = __consumer_state;
        consumer->__install_stop_callback(state);
        std::weak_ptr<typename State::consumer_base_t> weak_consumer{consumer};
        typename State::__consume_result consume_result;
        try {
            consume_result = state->__consume(std::move(weak_consumer));
        } catch (...) {
            consumer->__deliver_error(std::current_exception());
            state->__request_stop();
            return;
        }

        using consume_result_t = typename State::__consume_result;
        if (consume_result == consume_result_t::deliver_now) {
            consumer->__deliver(*state);
        } else if (consume_result == consume_result_t::already_consumed) {
            consumer->__deliver_stopped();
        }
    }
};

template<class State>
struct __sender {
    using sender_concept = sender_t;
    using state_t = State;

    explicit __sender(std::shared_ptr<State> state) noexcept
        : __state(std::move(state))
    {}

    __sender(__sender&& other) noexcept
        : __state(std::exchange(other.__state, nullptr))
    {}

    __sender& operator=(__sender&& other) noexcept {
        if (this != &other) {
            if (__state) {
                __state->__abandon_unconsumed();
            }
            __state = std::exchange(other.__state, nullptr);
        }
        return *this;
    }

    __sender(const __sender&) = delete;
    __sender& operator=(const __sender&) = delete;

    ~__sender() noexcept {
        if (__state) {
            __state->__abandon_unconsumed();
        }
    }

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> typename std::remove_cvref_t<Self>::state_t::output_cs_t {
        return {};
    }

    template<receiver R>
    auto connect(R rcvr) && -> __op<State, R> {
        return __op<State, R>{std::exchange(__state, nullptr), std::move(rcvr)};
    }

    auto get_env() const noexcept -> empty_env {
        return {};
    }

    std::shared_ptr<State> __state;
};

template<class State, class Env, class... Args>
[[nodiscard]] auto __make_state(Env& env, Args&&... args) {
    auto alloc = __allocator_for_env<Env>::get(env);
    using state_alloc_t = typename std::allocator_traits<decltype(alloc)>
        ::template rebind_alloc<State>;
    return std::allocate_shared<State>(
        state_alloc_t{alloc}, std::forward<Args>(args)..., std::move(alloc));
}

template<sender S, scope_token Token, queryable Env>
[[nodiscard]] auto __spawn_future(S sndr, Token token, Env env) {
    using association_t = decltype(token.try_associate());
    using state_t = __shared_state<S, Env, association_t>;
    auto association = token.try_associate();
    auto state = __make_state<state_t>(env, std::move(association));
    if (!state->__has_association()) {
        state->__complete_not_started();
        return __sender<state_t>{std::move(state)};
    }
    state->__start(std::move(sndr), std::move(env));
    return __sender<state_t>{std::move(state)};
}

} // namespace __forge_spawn_future

template<sender S, scope_token Token, queryable Env>
[[nodiscard]] auto spawn_future(S&& sndr, Token token, Env env) {
    auto wrapped = token.wrap(static_cast<S&&>(sndr));
    return __forge_spawn_future::__spawn_future(
        std::move(wrapped),
        std::move(token),
        std::move(env));
}

template<sender S, scope_token Token>
[[nodiscard]] auto spawn_future(S&& sndr, Token token) {
    auto wrapped = token.wrap(static_cast<S&&>(sndr));
    return __forge_spawn_future::__spawn_future(
        std::move(wrapped),
        std::move(token),
        empty_env{});
}

} // namespace std::execution
