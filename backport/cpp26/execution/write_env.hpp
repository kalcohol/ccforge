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
    [[no_unique_address]] OverrideEnv __override;
    [[no_unique_address]] BaseEnv __base;

    friend auto tag_invoke(get_stop_token_t, const __joined_env& self) noexcept {
        if constexpr (__forge_detail::tag_invocable<get_stop_token_t, const OverrideEnv&>) {
            return __forge_detail::tag_invoke_fn(get_stop_token_t{}, self.__override);
        } else if constexpr (__forge_detail::tag_invocable<get_stop_token_t, const BaseEnv&>) {
            return __forge_detail::tag_invoke_fn(get_stop_token_t{}, self.__base);
        } else {
            return std::never_stop_token{};
        }
    }

    template<class Tag>
        requires (!std::same_as<std::remove_cvref_t<Tag>, get_stop_token_t> &&
                  (__forge_detail::tag_invocable<Tag, const OverrideEnv&> ||
                   __forge_detail::tag_invocable<Tag, const BaseEnv&>))
    friend decltype(auto) tag_invoke(Tag tag, const __joined_env& self)
        noexcept((__forge_detail::tag_invocable<Tag, const OverrideEnv&> &&
                  __forge_detail::nothrow_tag_invocable<Tag, const OverrideEnv&>) ||
                 (!__forge_detail::tag_invocable<Tag, const OverrideEnv&> &&
                  __forge_detail::nothrow_tag_invocable<Tag, const BaseEnv&>)) {
        if constexpr (__forge_detail::tag_invocable<Tag, const OverrideEnv&>) {
            return __forge_detail::tag_invoke_fn(tag, self.__override);
        } else {
            return __forge_detail::tag_invoke_fn(tag, self.__base);
        }
    }
};

template<class OverrideEnv, class BaseEnv>
[[nodiscard]] auto __join_env(OverrideEnv&& override_env, BaseEnv&& base_env) {
    return __joined_env<std::decay_t<OverrideEnv>, std::decay_t<BaseEnv>>{
        static_cast<OverrideEnv&&>(override_env), static_cast<BaseEnv&&>(base_env)};
}

template<class OverrideEnv, class R>
struct __recv {
    using receiver_concept = receiver_t;

    R* __rcvr;
    OverrideEnv* __env;

    template<class... Vs>
    friend void tag_invoke(set_value_t, __recv&& self, Vs&&... vs) noexcept {
        set_value(std::move(*self.__rcvr), static_cast<Vs&&>(vs)...);
    }

    template<class E>
    friend void tag_invoke(set_error_t, __recv&& self, E&& e) noexcept {
        set_error(std::move(*self.__rcvr), static_cast<E&&>(e));
    }

    friend void tag_invoke(set_stopped_t, __recv&& self) noexcept {
        set_stopped(std::move(*self.__rcvr));
    }

    friend auto tag_invoke(get_env_t, const __recv& self) {
        return __join_env(*self.__env, std::execution::get_env(*self.__rcvr));
    }
};

template<class S, class OverrideEnv, class R>
struct __op : __forge_detail::__immovable {
    using operation_state_concept = operation_state_t;
    using recv_t = __recv<OverrideEnv, R>;
    using inner_op_t = connect_result_t<S, recv_t>;

    R __rcvr;
    OverrideEnv __env;
    inner_op_t __inner_op;

    __op(S sndr, OverrideEnv env, R rcvr)
        : __rcvr(std::move(rcvr))
        , __env(std::move(env))
        , __inner_op(std::execution::connect(
              std::move(sndr), recv_t{&__rcvr, &__env}))
    {}

    friend void tag_invoke(start_t, __op& self) noexcept {
        std::execution::start(self.__inner_op);
    }
};

template<class S, class OverrideEnv>
struct __sender {
    using sender_concept = sender_t;

    S __sndr;
    OverrideEnv __env;

    friend auto tag_invoke(get_completion_signatures_t,
                           const __sender& self, auto env) noexcept {
        using joined_env_t = decltype(__join_env(self.__env, env));
        using up_cs_t = decltype(std::execution::get_completion_signatures(
            self.__sndr, std::declval<joined_env_t>()));
        return up_cs_t{};
    }

    template<receiver R>
    friend auto tag_invoke(connect_t, __sender&& self, R r)
        -> __op<S, OverrideEnv, R>
    {
        return __op<S, OverrideEnv, R>{
            std::move(self.__sndr), std::move(self.__env), std::move(r)};
    }

    template<receiver R>
        requires std::copy_constructible<S> && std::copy_constructible<OverrideEnv>
    friend auto tag_invoke(connect_t, const __sender& self, R r)
        -> __op<S, OverrideEnv, R>
    {
        return __op<S, OverrideEnv, R>{self.__sndr, self.__env, std::move(r)};
    }

    friend auto tag_invoke(get_env_t, const __sender& self) noexcept {
        return std::execution::get_env(self.__sndr);
    }
};

template<class OverrideEnv>
struct __closure {
    OverrideEnv __env;

    template<sender S>
        requires std::copy_constructible<OverrideEnv>
    [[nodiscard]] auto operator()(S&& sndr) const & {
        return __sender<std::decay_t<S>, OverrideEnv>{std::forward<S>(sndr), __env};
    }

    template<sender S>
    [[nodiscard]] auto operator()(S&& sndr) && {
        return __sender<std::decay_t<S>, OverrideEnv>{
            std::forward<S>(sndr), std::move(__env)};
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
            std::forward<S>(sndr), std::forward<Env>(env)};
    }

    template<queryable Env>
    [[nodiscard]] auto operator()(Env&& env) const {
        return __closure<std::decay_t<Env>>{std::forward<Env>(env)};
    }
};

} // namespace __forge_write_env

inline constexpr __forge_write_env::__write_env_t write_env{};

} // namespace std::execution
