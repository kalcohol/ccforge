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

#include <concepts>
#include <type_traits>
#include <utility>

namespace std::execution {

namespace __forge_write_env {

template<class OverrideEnv, class BaseEnv>
struct __joined_env {
    const OverrideEnv* __overlay_env;
    const BaseEnv* __base;

    template<class Tag>
        requires (__forge_env_detail::__queryable<Tag, const OverrideEnv&> ||
                  __forge_env_detail::__queryable<Tag, const BaseEnv&>)
    friend decltype(auto) tag_invoke(Tag tag, const __joined_env& self)
        noexcept((__forge_env_detail::__queryable<Tag, const OverrideEnv&> &&
                  __forge_env_detail::__nothrow_query<Tag, const OverrideEnv&>) ||
                 (!__forge_env_detail::__queryable<Tag, const OverrideEnv&> &&
                  __forge_env_detail::__nothrow_query<Tag, const BaseEnv&>)) {
        if constexpr (__forge_env_detail::__queryable<Tag, const OverrideEnv&>) {
            return __forge_env_detail::__query(tag, *self.__overlay_env);
        } else {
            return __forge_env_detail::__query(tag, *self.__base);
        }
    }
};

template<class OverrideEnv, class BaseEnv>
[[nodiscard]] auto __join_env(
    const OverrideEnv& override_env,
    const BaseEnv& base_env) noexcept {
    return __joined_env<OverrideEnv, BaseEnv>{&override_env, &base_env};
}

template<class OverrideEnv, class BaseEnv, class R>
struct __recv {
    using receiver_concept = receiver_t;

    R* __rcvr;
    OverrideEnv* __env;
    BaseEnv* __base_env;

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
        return __join_env(*__env, *__base_env);
    }
};

template<class S, class OverrideEnv, class R>
struct __op : __forge_detail::__immovable {
    using operation_state_concept = operation_state_t;
    using base_env_t = std::decay_t<env_of_t<R>>;
    using recv_t = __recv<OverrideEnv, base_env_t, R>;
    using inner_op_t = connect_result_t<S, recv_t>;

    R __rcvr;
    OverrideEnv __env;
    base_env_t __base_env;
    inner_op_t __inner_op;

    __op(S sndr, OverrideEnv env, R rcvr)
        : __rcvr(std::move(rcvr))
        , __env(std::move(env))
        , __base_env(std::execution::get_env(__rcvr))
        , __inner_op(std::execution::connect(
              std::move(sndr), recv_t{&__rcvr, &__env, &__base_env}))
    {}

    void start() & noexcept {
        std::execution::start(__inner_op);
    }
};

template<class S, class OverrideEnv>
struct __sender {
    using sender_concept = sender_t;
    using source_t = S;
    using override_env_t = OverrideEnv;

    S __sndr;
    OverrideEnv __env;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept {
        using self_t = std::remove_cvref_t<Self>;
        using joined_env_t = __joined_env<
            typename self_t::override_env_t,
            std::decay_t<Env>>;
        using up_cs_t = decltype(std::execution::get_completion_signatures(
            std::declval<typename self_t::source_t>(),
            std::declval<joined_env_t>()));
        return up_cs_t{};
    }

    template<receiver R>
    auto connect(R r) && -> __op<S, OverrideEnv, R> {
        return __op<S, OverrideEnv, R>{
            std::move(__sndr), std::move(__env), std::move(r)};
    }

    template<receiver R>
        requires std::copy_constructible<S> && std::copy_constructible<OverrideEnv>
    auto connect(R r) const& -> __op<S, OverrideEnv, R> {
        return __op<S, OverrideEnv, R>{__sndr, __env, std::move(r)};
    }

    auto get_env() const noexcept {
        return std::execution::get_env(__sndr);
    }
};

template<class OverrideEnv>
struct __closure {
    OverrideEnv __env;

    template<sender S>
        requires std::copy_constructible<OverrideEnv>
    [[nodiscard]] auto operator()(S&& sndr) const & {
        return __sender<std::decay_t<S>, OverrideEnv>{
            __forge_detail::__forward_as_given(std::forward<S>(sndr)), __env};
    }

    template<sender S>
    [[nodiscard]] auto operator()(S&& sndr) && {
        return __sender<std::decay_t<S>, OverrideEnv>{
            __forge_detail::__forward_as_given(std::forward<S>(sndr)),
            std::move(__env)};
    }

    template<sender S>
        requires std::copy_constructible<OverrideEnv>
    friend constexpr auto operator|(S&& sndr, const __closure& self) {
        return self(std::forward<S>(sndr));
    }

    template<sender S>
    friend constexpr auto operator|(S&& sndr, __closure&& self) {
        return std::move(self)(std::forward<S>(sndr));
    }
};

struct __write_env_t {
    template<sender S, queryable Env>
    [[nodiscard]] auto operator()(S&& sndr, Env&& env) const {
        return __sender<std::decay_t<S>, std::decay_t<Env>>{
            __forge_detail::__forward_as_given(std::forward<S>(sndr)),
            std::forward<Env>(env)};
    }

    template<queryable Env>
    [[nodiscard]] auto operator()(Env&& env) const {
        return __closure<std::decay_t<Env>>{std::forward<Env>(env)};
    }
};

} // namespace __forge_write_env

inline constexpr __forge_write_env::__write_env_t write_env{};

} // namespace std::execution
