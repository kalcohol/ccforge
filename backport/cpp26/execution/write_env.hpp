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
#include "detail/adaptor.hpp"

#include <concepts>
#include <type_traits>
#include <utility>

namespace std::execution {

namespace __forge_write_env {

template<class Query, class OverrideEnv>
concept __from_overlay = requires(const OverrideEnv& overlay, Query&& q) {
    overlay.query(static_cast<Query&&>(q));
};

template<class Query, class OverrideEnv>
concept __nothrow_from_overlay = requires(const OverrideEnv& overlay, Query&& q) {
    { overlay.query(static_cast<Query&&>(q)) } noexcept;
};

// join-env(state, FWD-ENV(env)) of [exec.write.env]/3-4: the overlay answers
// first as a const lvalue, then the FWD-ENV view of the base environment.
// BaseEnv is the type of the base environment without reference and const.
template<class OverrideEnv, class BaseEnv>
struct __joined_view {
    using __base_t = __forge_env_detail::__fwd_env_view<BaseEnv>;

    template<class Query>
    static constexpr bool __from_base = requires(const __base_t& base, Query&& q) {
        base.query(static_cast<Query&&>(q));
    };

    template<class Query>
    static consteval bool __query_nothrow() {
        if constexpr (__from_overlay<Query, OverrideEnv>) {
            return __nothrow_from_overlay<Query, OverrideEnv>;
        } else {
            return noexcept(std::declval<const __base_t&>().query(std::declval<Query>()));
        }
    }

    const OverrideEnv* __overlay_env;
    __base_t __base;

    template<class Tag>
        requires __from_overlay<Tag, OverrideEnv> || __from_base<Tag>
    constexpr decltype(auto) query(Tag&& tag) const noexcept(__query_nothrow<Tag>()) {
        if constexpr (__from_overlay<Tag, OverrideEnv>) {
            return __overlay_env->query(static_cast<Tag&&>(tag));
        } else {
            return __base.query(static_cast<Tag&&>(tag));
        }
    }
};

template<class OverrideEnv, class BaseEnv>
using __joined_env = __joined_view<
    OverrideEnv, __forge_env_detail::__normalized_env_t<BaseEnv>>;

template<class OverrideEnv, class R>
struct __recv {
    using receiver_concept = receiver_tag;

    R* __rcvr;
    OverrideEnv* __env;
    // The receiver environment that the operation state keeps.
    const __forge_env_detail::__receiver_env<R>* __base_env = nullptr;

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

    auto get_env() const noexcept -> __joined_env<OverrideEnv, env_of_t<R>> {
        return {__env, __base_env->__view()};
    }
};

template<class S, class OverrideEnv, class R>
struct __op : __forge_detail::__immovable {
    using operation_state_concept = operation_state_tag;
    using recv_t = __recv<OverrideEnv, R>;
    using inner_op_t = connect_result_t<S, recv_t>;

    R __rcvr;
    OverrideEnv __env;
    // Captured after the overlay, as at 8de815c; outlives every view of the
    // joined environment.
    __forge_env_detail::__receiver_env<R> __base_env;
    inner_op_t __inner_op;

    template<class Data>
    __op(S&& sndr, Data&& env, R rcvr)
        noexcept(std::is_nothrow_move_constructible_v<R> &&
                 noexcept(OverrideEnv(__forge_detail::__allocator_aware_forward(
                     static_cast<Data&&>(env), rcvr))) &&
                 noexcept(std::execution::connect(std::declval<S>(), std::declval<recv_t>())))
        : __rcvr(std::move(rcvr))
        , __env(__forge_detail::__allocator_aware_forward(static_cast<Data&&>(env), __rcvr))
        , __base_env(__rcvr)
        , __inner_op(std::execution::connect(
              static_cast<S&&>(sndr), recv_t{std::addressof(__rcvr), std::addressof(__env),
                  std::addressof(__base_env)}))
    {}

    void start() & noexcept {
        std::execution::start(__inner_op);
    }
};

template<class S, class OverrideEnv>
struct __sender {
    using sender_concept = sender_tag;
    using source_t = S;
    using override_env_t = OverrideEnv;

    S __sndr;
    OverrideEnv __env;

    template<class Child, class Env>
    using __cs = completion_signatures_of_t<Child, __joined_env<OverrideEnv, Env>>;

    template<class Self, class Env>
    static constexpr auto get_completion_signatures() noexcept
        -> __cs<__forge_detail::__child_category_t<Self, S>, Env> { return {}; }

    template<receiver R>
        requires requires { typename __cs<S, env_of_t<R>>;
                     typename connect_result_t<S, __recv<OverrideEnv, R>>; }
    auto connect(R r) &&
        noexcept(noexcept(__op<S, OverrideEnv, R>{
            std::move(__sndr), std::move(__env), std::move(r)})) -> __op<S, OverrideEnv, R> {
        return __op<S, OverrideEnv, R>{
            std::move(__sndr), std::move(__env), std::move(r)};
    }

    template<receiver R>
        requires std::copy_constructible<S> && std::copy_constructible<OverrideEnv> &&
                 requires { typename __cs<const S&, env_of_t<R>>;
                     typename connect_result_t<const S&, __recv<OverrideEnv, R>>; }
    auto connect(R r) const&
        noexcept(noexcept(__op<const S&, OverrideEnv, R>{
            __sndr, __env, std::move(r)})) -> __op<const S&, OverrideEnv, R> {
        return __op<const S&, OverrideEnv, R>{__sndr, __env, std::move(r)};
    }

    // An invalid rvalue child connection must not retry the const-lvalue path.
    template<class R> void connect(R) && = delete;

    decltype(auto) get_env() const noexcept {
        return std::execution::get_env(__sndr);
    }
};

struct __write_env_t {
    template<sender S, __forge_queryable Env>
    [[nodiscard]] constexpr auto operator()(S&& sndr, Env&& env) const {
        return __sender<std::decay_t<S>, std::decay_t<Env>>{
            __forge_detail::__forward_as_given(std::forward<S>(sndr)),
            std::forward<Env>(env)};
    }
};

} // namespace __forge_write_env

inline constexpr __forge_write_env::__write_env_t write_env{};

} // namespace std::execution
