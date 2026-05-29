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

#include <exception>
#include <functional>
#include <type_traits>
#include <utility>

namespace std::execution {

namespace __forge_let {

struct __value_tag  {};
struct __error_tag  {};
struct __stopped_tag{};

template<class Sig>
struct __is_value_sig : std::false_type {};

template<class... Vs>
struct __is_value_sig<set_value_t(Vs...)> : std::true_type {};

template<class Sig>
struct __is_error_sig : std::false_type {};

template<class E>
struct __is_error_sig<set_error_t(E)> : std::true_type {};

template<class Which, class Sig>
struct __matches_which : std::false_type {};

template<class... Vs>
struct __matches_which<__value_tag, set_value_t(Vs...)> : std::true_type {};

template<class E>
struct __matches_which<__error_tag, set_error_t(E)> : std::true_type {};

template<>
struct __matches_which<__stopped_tag, set_stopped_t()> : std::true_type {};

template<bool Enabled>
struct __maybe_eptr_cs {
    using type = completion_signatures<>;
};

template<>
struct __maybe_eptr_cs<true> {
    using type = completion_signatures<set_error_t(std::exception_ptr)>;
};

template<class Fn, class Which, class Env, class Sig>
struct __let_sig {
    using type = completion_signatures<Sig>;
};

template<class Fn, class Env, class... Vs>
struct __let_sig<Fn, __value_tag, Env, set_value_t(Vs...)> {
    using inner_sender_t = std::invoke_result_t<Fn, Vs...>;
    using type = decltype(std::execution::get_completion_signatures(
        std::declval<inner_sender_t>(), std::declval<Env>()));
};

template<class Fn, class Env, class E>
struct __let_sig<Fn, __error_tag, Env, set_error_t(E)> {
    using inner_sender_t = std::invoke_result_t<Fn, E>;
    using type = decltype(std::execution::get_completion_signatures(
        std::declval<inner_sender_t>(), std::declval<Env>()));
};

template<class Fn, class Env>
struct __let_sig<Fn, __stopped_tag, Env, set_stopped_t()> {
    using inner_sender_t = std::invoke_result_t<Fn>;
    using type = decltype(std::execution::get_completion_signatures(
        std::declval<inner_sender_t>(), std::declval<Env>()));
};

template<class Fn, class Which, class Env, class CS>
struct __let_cs;

template<class Fn, class Which, class Env, class... Sigs>
struct __let_cs<Fn, Which, Env, completion_signatures<Sigs...>> {
    static constexpr bool handles_completion = (__matches_which<Which, Sigs>::value || ...);

    using type = __forge_meta::__concat_unique_cs_t<
        typename __let_sig<Fn, Which, Env, Sigs>::type...,
        typename __maybe_eptr_cs<handles_completion>::type>;
};

template<class S, class Fn, class R, class Which>
struct __op : __forge_detail::__immovable {
    using operation_state_concept = operation_state_t;

    R __outer_recv_;
    Fn __fn_;

    __forge_detail::__op_storage<1024> __outer_storage_;
    __forge_detail::__op_storage<1024> __inner_storage_;

    struct __inner_recv {
        using receiver_concept = receiver_t;
        __op* __self;
        template<class... Vs>
        void set_value(Vs&&... vs) && noexcept {
            std::execution::set_value(std::move(__self->__outer_recv_), static_cast<Vs&&>(vs)...);
        }
        template<class E>
        void set_error(E&& e) && noexcept {
            std::execution::set_error(std::move(__self->__outer_recv_), static_cast<E&&>(e));
        }
        void set_stopped() && noexcept {
            std::execution::set_stopped(std::move(__self->__outer_recv_));
        }
        auto get_env() const noexcept -> env_of_t<R> {
            return std::execution::get_env(__self->__outer_recv_);
        }
    };

    template<class... Vs>
    void __start_inner(Vs&&... vs) noexcept {
        using inner_sender_t = std::decay_t<std::invoke_result_t<Fn, std::decay_t<Vs>...>>;
        using inner_op_t = connect_result_t<inner_sender_t, __inner_recv>;
        try {
            auto inner_sndr = std::invoke(std::move(__fn_), static_cast<Vs&&>(vs)...);
            auto* op = __inner_storage_.template emplace_from<inner_op_t>([&]() -> inner_op_t {
                return std::execution::connect(std::move(inner_sndr), __inner_recv{this});
            });
            std::execution::start(*op);
        } catch (...) {
            set_error(std::move(__outer_recv_), std::current_exception());
        }
    }

    struct __outer_recv {
        using receiver_concept = receiver_t;
        __op* __self;

        template<class... Vs>
        void set_value(Vs&&... vs) && noexcept {
            if constexpr (std::is_same_v<Which, __value_tag>) {
                __self->__start_inner(static_cast<Vs&&>(vs)...);
            } else {
                std::execution::set_value(std::move(__self->__outer_recv_), static_cast<Vs&&>(vs)...);
            }
        }
        template<class E>
        void set_error(E&& e) && noexcept {
            if constexpr (std::is_same_v<Which, __error_tag>) {
                __self->__start_inner(static_cast<E&&>(e));
            } else {
                std::execution::set_error(std::move(__self->__outer_recv_), static_cast<E&&>(e));
            }
        }
        void set_stopped() && noexcept {
            if constexpr (std::is_same_v<Which, __stopped_tag>) {
                __self->__start_inner();
            } else {
                std::execution::set_stopped(std::move(__self->__outer_recv_));
            }
        }
        auto get_env() const noexcept -> env_of_t<R> {
            return std::execution::get_env(__self->__outer_recv_);
        }
    };

    using __outer_op_t = connect_result_t<S, __outer_recv>;

    __op(S sndr, Fn fn, R r)
        : __outer_recv_(std::move(r))
        , __fn_(std::move(fn))
    {
        __outer_storage_.template emplace_from<__outer_op_t>([&]() -> __outer_op_t {
            return std::execution::connect(std::move(sndr), __outer_recv{this});
        });
    }

    void start() & noexcept {
        std::execution::start(__outer_storage_.template get<__outer_op_t>());
    }
};

template<class S, class Fn, class Which>
struct __sender {
    using sender_concept = sender_t;
    using source_t = S;
    using fn_t = Fn;
    using which_t = Which;

    S __sndr;
    Fn __fn;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept {
        using self_t = std::remove_cvref_t<Self>;
        using up_cs_t = decltype(std::execution::get_completion_signatures(
            std::declval<const typename self_t::source_t&>(),
            std::declval<Env>()));
        return typename __let_cs<
            typename self_t::fn_t,
            typename self_t::which_t,
            Env,
            up_cs_t>::type{};
    }

    template<receiver R>
    auto connect(R r) && -> __op<S, Fn, R, Which>
    {
        return __op<S, Fn, R, Which>(
            std::move(__sndr), std::move(__fn), std::move(r));
    }

    template<receiver R>
        requires std::copy_constructible<S> && std::copy_constructible<Fn>
    auto connect(R r) const& -> __op<S, Fn, R, Which>
    {
        return __op<S, Fn, R, Which>(
            __sndr, __fn, std::move(r));
    }

    auto get_env() const noexcept {
        return std::execution::get_env(__sndr);
    }
};

template<class Fn, class Which>
struct __let_closure {
    Fn __fn;
    template<sender S>
    [[nodiscard]] auto operator()(S&& s) const & {
        return __sender<std::decay_t<S>, Fn, Which>{std::forward<S>(s), __fn};
    }
    template<sender S>
    [[nodiscard]] auto operator()(S&& s) && {
        return __sender<std::decay_t<S>, Fn, Which>{std::forward<S>(s), std::move(__fn)};
    }
    template<sender S>
    friend constexpr auto operator|(S&& s, const __let_closure& self) {
        return self(std::forward<S>(s));
    }
    template<sender S>
    friend constexpr auto operator|(S&& s, __let_closure&& self) {
        return std::move(self)(std::forward<S>(s));
    }
};

template<class Which>
struct __let_t {
    template<sender S, class Fn>
    [[nodiscard]] auto operator()(S&& s, Fn&& fn) const {
        return __sender<std::decay_t<S>, std::decay_t<Fn>, Which>{
            std::forward<S>(s), std::forward<Fn>(fn)};
    }
    template<class Fn>
    [[nodiscard]] auto operator()(Fn&& fn) const {
        return __let_closure<std::decay_t<Fn>, Which>{std::forward<Fn>(fn)};
    }
};

} // namespace __forge_let

inline constexpr __forge_let::__let_t<__forge_let::__value_tag>   let_value{};
inline constexpr __forge_let::__let_t<__forge_let::__error_tag>   let_error{};
inline constexpr __forge_let::__let_t<__forge_let::__stopped_tag> let_stopped{};

} // namespace std::execution
