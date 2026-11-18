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
#include "detail.hpp"
#include "detail/adaptor.hpp"

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

namespace std::execution {

struct schedule_from_t {
    template<sender S>
    [[nodiscard]] constexpr auto operator()(S&& sndr) const;
};

inline constexpr schedule_from_t schedule_from{};

namespace __forge_schedule_from {

template<class Env>
using __fwd_env_t = __forge_env_detail::__fwd_env_view<Env>;

struct __data {};

template<class R>
struct __recv {
    using receiver_concept = receiver_tag;
    R* __outer;
    // The environment that the operation state keeps for this receiver.
    const __forge_env_detail::__receiver_env<R>* __env = nullptr;
    template<class... As> void set_value(As&&... as) && noexcept {
        std::execution::set_value(std::move(*__outer), static_cast<As&&>(as)...);
    }
    template<class E> void set_error(E&& e) && noexcept {
        std::execution::set_error(std::move(*__outer), static_cast<E&&>(e));
    }
    void set_stopped() && noexcept { std::execution::set_stopped(std::move(*__outer)); }
    auto get_env() const noexcept -> __fwd_env_t<env_of_t<R>> {
        return __env->__view();
    }
};

template<class S>
struct __sender {
    using sender_concept = sender_tag;
    using source_t = S;

    [[no_unique_address]] __data __data_;
    [[no_unique_address]] S __sndr_;

    template<class Self, class Env>
    static constexpr auto get_completion_signatures() noexcept
        -> decltype(std::execution::get_completion_signatures(
            std::declval<__forge_detail::__child_category_t<Self, S>>(),
            std::declval<__fwd_env_t<Env>>())) {
        return {};
    }

    auto get_env() const noexcept -> __forge_env_detail::__fwd_attrs_view<env_of_t<S>> {
        return __forge_env_detail::__fwd_attrs_view<env_of_t<S>>::__of(__sndr_);
    }

    template<class Child, class R>
    using __op = __forge_detail::__receiver_op<Child, R, __recv<R>>;

    template<receiver R>
        requires sender_in<S, __fwd_env_t<env_of_t<R>>> &&
                 requires { typename connect_result_t<S, __recv<R>>; }
    auto connect(R rcvr) &&
        noexcept(noexcept(__op<S, R>(std::move(__sndr_), std::move(rcvr)))) -> __op<S, R> {
        return __op<S, R>(std::move(__sndr_), std::move(rcvr));
    }

    template<receiver R>
        requires std::copy_constructible<S> && sender_in<const S&, __fwd_env_t<env_of_t<R>>> &&
                 requires { typename connect_result_t<const S&, __recv<R>>; }
    auto connect(R rcvr) const&
        noexcept(noexcept(__op<const S&, R>(__sndr_, std::move(rcvr)))) -> __op<const S&, R> {
        return __op<const S&, R>(__sndr_, std::move(rcvr));
    }

    // An invalid rvalue child connection must not retry the const-lvalue path.
    template<class R> void connect(R) && = delete;

    template<std::size_t I>
    constexpr decltype(auto) get() & noexcept {
        static_assert(I < 3);
        if constexpr (I == 0) {
            return schedule_from_t{};
        } else if constexpr (I == 1) {
            return (__data_);
        } else {
            return (__sndr_);
        }
    }

    template<std::size_t I>
    constexpr decltype(auto) get() const& noexcept {
        static_assert(I < 3);
        if constexpr (I == 0) {
            return schedule_from_t{};
        } else if constexpr (I == 1) {
            return (__data_);
        } else {
            return (__sndr_);
        }
    }

    template<std::size_t I>
    constexpr decltype(auto) get() && noexcept {
        static_assert(I < 3);
        if constexpr (I == 0) {
            return schedule_from_t{};
        } else if constexpr (I == 1) {
            return std::move(__data_);
        } else {
            return std::move(__sndr_);
        }
    }

    template<std::size_t I>
    constexpr decltype(auto) get() const&& noexcept {
        static_assert(I < 3);
        if constexpr (I == 0) {
            return schedule_from_t{};
        } else if constexpr (I == 1) {
            return std::move(__data_);
        } else {
            return std::move(__sndr_);
        }
    }
};

} // namespace __forge_schedule_from

template<sender S>
[[nodiscard]] constexpr auto schedule_from_t::operator()(S&& sndr) const {
    return __forge_schedule_from::__sender<std::decay_t<S>>{
        {},
        __forge_detail::__forward_as_given(std::forward<S>(sndr))};
}

} // namespace std::execution

namespace std {

template<class S>
struct tuple_size<execution::__forge_schedule_from::__sender<S>>
    : integral_constant<size_t, 3> {};

template<size_t I, class S>
struct tuple_element<I, execution::__forge_schedule_from::__sender<S>>
    : tuple_element<I, tuple<
          execution::schedule_from_t,
          execution::__forge_schedule_from::__data,
          S>> {};

} // namespace std
