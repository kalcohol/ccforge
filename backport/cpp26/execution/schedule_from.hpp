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

struct __data {};

template<class R>
struct __recv {
    using receiver_concept = receiver_tag;
    R __outer;
    template<class... As> void set_value(As&&... as) && noexcept {
        std::execution::set_value(std::move(__outer), static_cast<As&&>(as)...);
    }
    template<class E> void set_error(E&& e) && noexcept {
        std::execution::set_error(std::move(__outer), static_cast<E&&>(e));
    }
    void set_stopped() && noexcept { std::execution::set_stopped(std::move(__outer)); }
    auto get_env() const noexcept -> __forge_detail::__fwd_env_t<env_of_t<R>> {
        return {std::execution::get_env(__outer)};
    }
};

template<class S>
struct __sender {
    using sender_concept = sender_tag;
    using source_t = S;

    [[no_unique_address]] __data __data_;
    [[no_unique_address]] S __sndr_;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> decltype(std::execution::get_completion_signatures(
            std::declval<__forge_detail::__child_category_t<Self, S>>(),
            std::declval<__forge_detail::__fwd_env_t<Env>>())) {
        return {};
    }

    auto get_env() const noexcept -> __forge_detail::__fwd_env_t<env_of_t<S>> {
        return {std::execution::get_env(__sndr_)};
    }

    template<receiver R>
        requires sender_in<S, __forge_detail::__fwd_env_t<env_of_t<R>>>
    auto connect(R rcvr) &&
        noexcept(noexcept(std::execution::connect(std::move(__sndr_), __recv<R>{std::move(rcvr)})))
        -> connect_result_t<S, __recv<R>> {
        return std::execution::connect(std::move(__sndr_), __recv<R>{std::move(rcvr)});
    }

    template<receiver R>
        requires std::copy_constructible<S> && sender_in<const S&, __forge_detail::__fwd_env_t<env_of_t<R>>>
    auto connect(R rcvr) const&
        noexcept(noexcept(std::execution::connect(__sndr_, __recv<R>{std::move(rcvr)})))
        -> connect_result_t<const S&, __recv<R>> {
        return std::execution::connect(__sndr_, __recv<R>{std::move(rcvr)});
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
