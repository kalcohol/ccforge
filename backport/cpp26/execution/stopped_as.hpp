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

#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

namespace std::execution {

namespace __forge_stopped {

template<class CS>
struct __first_optional_type {
    using type = std::optional<std::tuple<>>;
};

template<class... Vs, class... Rest>
struct __first_optional_type<completion_signatures<set_value_t(Vs...), Rest...>> {
    using type = std::optional<std::tuple<std::decay_t<Vs>...>>;
};

template<class Other, class... Rest>
struct __first_optional_type<completion_signatures<Other, Rest...>>
    : __first_optional_type<completion_signatures<Rest...>> {};

template<class Sig>
struct __optional_sig {
    using type = completion_signatures<Sig>;
};

template<class... Vs>
struct __optional_sig<set_value_t(Vs...)> {
    using type = completion_signatures<
        set_value_t(std::optional<std::tuple<std::decay_t<Vs>...>>)>;
};

template<>
struct __optional_sig<set_stopped_t()> {
    using type = completion_signatures<>;
};

template<class OptionalT, bool SendsStopped>
struct __optional_stopped_sig {
    using type = completion_signatures<>;
};

template<class OptionalT>
struct __optional_stopped_sig<OptionalT, true> {
    using type = completion_signatures<set_value_t(OptionalT)>;
};

template<class CS>
struct __optional_cs;

template<class... Sigs>
struct __optional_cs<completion_signatures<Sigs...>> {
    using optional_t = typename __first_optional_type<completion_signatures<Sigs...>>::type;
    using type = __forge_meta::__concat_unique_cs_t<
        typename __optional_sig<Sigs>::type...,
        typename __optional_stopped_sig<optional_t, __forge_meta::has_stopped_v<Sigs...>>::type>;
};

template<class Err, bool SendsStopped>
struct __error_stopped_sig {
    using type = completion_signatures<>;
};

template<class Err>
struct __error_stopped_sig<Err, true> {
    using type = completion_signatures<set_error_t(Err)>;
};

template<class Sig>
struct __drop_stopped_sig {
    using type = completion_signatures<Sig>;
};

template<>
struct __drop_stopped_sig<set_stopped_t()> {
    using type = completion_signatures<>;
};

template<class CS, class Err>
struct __error_cs;

template<class Err, class... Sigs>
struct __error_cs<completion_signatures<Sigs...>, Err> {
    using type = __forge_meta::__concat_unique_cs_t<
        typename __drop_stopped_sig<Sigs>::type...,
        typename __error_stopped_sig<std::decay_t<Err>, __forge_meta::has_stopped_v<Sigs...>>::type>;
};

template<class S, class R, class OptionalT>
struct __optional_op : __forge_detail::__immovable {
    using operation_state_concept = operation_state_t;

    struct __recv {
        using receiver_concept = receiver_t;
        R* __rcvr;

        template<class... Vs>
        friend void tag_invoke(set_value_t, __recv&& self, Vs&&... vs) noexcept {
            using val_t = std::optional<std::tuple<std::decay_t<Vs>...>>;
            set_value(std::move(*self.__rcvr), val_t(std::tuple(static_cast<Vs&&>(vs)...)));
        }
        template<class E>
        friend void tag_invoke(set_error_t, __recv&& self, E&& e) noexcept {
            set_error(std::move(*self.__rcvr), static_cast<E&&>(e));
        }
        friend void tag_invoke(set_stopped_t, __recv&& self) noexcept {
            set_value(std::move(*self.__rcvr), OptionalT{std::nullopt});
        }
        friend auto tag_invoke(get_env_t, const __recv& self) noexcept
            -> env_of_t<R> {
            return std::execution::get_env(*self.__rcvr);
        }
    };

    using inner_op_t = connect_result_t<S, __recv>;

    R __rcvr;
    inner_op_t __op;

    __optional_op(S sndr, R r)
        : __rcvr(std::move(r))
        , __op(std::execution::connect(std::move(sndr), __recv{&__rcvr}))
    {}

    friend void tag_invoke(start_t, __optional_op& self) noexcept {
        std::execution::start(self.__op);
    }
};

template<class S>
struct __optional_sender {
    using sender_concept = sender_t;
    S __sndr;

    template<receiver R>
    friend auto tag_invoke(connect_t, __optional_sender self, R r)
        -> __optional_op<S, R,
            typename __first_optional_type<decltype(std::execution::get_completion_signatures(
                std::declval<S>(), std::declval<env_of_t<R>>()))>::type>
    {
        using cs_t = decltype(std::execution::get_completion_signatures(
            std::declval<S>(), std::declval<env_of_t<R>>()));
        using optional_t = typename __first_optional_type<cs_t>::type;
        return __optional_op<S, R, optional_t>(std::move(self.__sndr), std::move(r));
    }

    friend auto tag_invoke(get_completion_signatures_t,
                           const __optional_sender& self, auto env) noexcept {
        using up_cs_t = decltype(std::execution::get_completion_signatures(self.__sndr, env));
        return typename __optional_cs<up_cs_t>::type{};
    }

    friend auto tag_invoke(get_env_t, const __optional_sender& self) noexcept {
        return std::execution::get_env(self.__sndr);
    }
};

template<class S, class Err, class R>
struct __error_op : __forge_detail::__immovable {
    using operation_state_concept = operation_state_t;

    struct __recv {
        using receiver_concept = receiver_t;
        R* __rcvr;
        Err __err;

        template<class... Vs>
        friend void tag_invoke(set_value_t, __recv&& self, Vs&&... vs) noexcept {
            set_value(std::move(*self.__rcvr), static_cast<Vs&&>(vs)...);
        }
        template<class E>
        friend void tag_invoke(set_error_t, __recv&& self, E&& e) noexcept {
            set_error(std::move(*self.__rcvr), static_cast<E&&>(e));
        }
        friend void tag_invoke(set_stopped_t, __recv&& self) noexcept {
            set_error(std::move(*self.__rcvr), std::move(self.__err));
        }
        friend auto tag_invoke(get_env_t, const __recv& self) noexcept
            -> env_of_t<R> {
            return std::execution::get_env(*self.__rcvr);
        }
    };

    using inner_op_t = connect_result_t<S, __recv>;

    R __rcvr;
    inner_op_t __op;

    __error_op(S sndr, Err err, R r)
        : __rcvr(std::move(r))
        , __op(std::execution::connect(
            std::move(sndr), __recv{&__rcvr, std::move(err)}))
    {}

    friend void tag_invoke(start_t, __error_op& self) noexcept {
        std::execution::start(self.__op);
    }
};

template<class S, class Err>
struct __error_sender {
    using sender_concept = sender_t;
    S __sndr;
    Err __err;

    template<receiver R>
    friend auto tag_invoke(connect_t, __error_sender self, R r)
        -> __error_op<S, Err, R>
    {
        return __error_op<S, Err, R>(
            std::move(self.__sndr), std::move(self.__err), std::move(r));
    }

    friend auto tag_invoke(get_completion_signatures_t,
                           const __error_sender& self, auto env) noexcept {
        using up_cs_t = decltype(std::execution::get_completion_signatures(self.__sndr, env));
        return typename __error_cs<up_cs_t, Err>::type{};
    }

    friend auto tag_invoke(get_env_t, const __error_sender& self) noexcept {
        return std::execution::get_env(self.__sndr);
    }
};

} // namespace __forge_stopped

template<sender S>
[[nodiscard]] auto stopped_as_optional(S sndr) {
    return __forge_stopped::__optional_sender<S>{std::move(sndr)};
}

template<sender S, class Err>
[[nodiscard]] auto stopped_as_error(S sndr, Err err) {
    return __forge_stopped::__error_sender<S, Err>{
        std::move(sndr), std::move(err)};
}

} // namespace std::execution
