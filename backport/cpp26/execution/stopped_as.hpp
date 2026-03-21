// MIT License
//
// Copyright (c) 2026 Forge Project
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

namespace std::execution {

namespace __forge_stopped {

template<class S, class R>
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
            set_value(std::move(*self.__rcvr), std::nullopt);
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
        -> __optional_op<S, R>
    {
        return __optional_op<S, R>(std::move(self.__sndr), std::move(r));
    }

    friend auto tag_invoke(get_completion_signatures_t,
                           const __optional_sender& self, auto env) noexcept {
        return decltype(std::execution::get_completion_signatures(self.__sndr, env)){};
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
        return decltype(std::execution::get_completion_signatures(self.__sndr, env)){};
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
