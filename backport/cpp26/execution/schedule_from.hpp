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

#include <cstddef>
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

template<class S>
struct __sender {
    using sender_concept = sender_t;
    using source_t = S;

    [[no_unique_address]] __data __data_;
    [[no_unique_address]] S __sndr_;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> decltype(std::execution::get_completion_signatures(
            std::declval<typename std::remove_cvref_t<Self>::source_t>(),
            std::declval<Env>())) {
        return {};
    }

    auto get_env() const noexcept -> env_of_t<S> {
        return std::execution::get_env(__sndr_);
    }

    template<receiver R>
    auto connect(R rcvr) &&
        noexcept(noexcept(std::execution::connect(
            std::move(__sndr_), std::move(rcvr)))) {
        return std::execution::connect(
            std::move(__sndr_), std::move(rcvr));
    }

    template<receiver R>
        requires std::copy_constructible<S>
    auto connect(R rcvr) const&
        noexcept(noexcept(std::execution::connect(
            S(__sndr_), std::move(rcvr)))) {
        return std::execution::connect(
            S(__sndr_), std::move(rcvr));
    }

    template<std::size_t I>
    decltype(auto) get() & noexcept {
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
    decltype(auto) get() const& noexcept {
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
    decltype(auto) get() && noexcept {
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
    decltype(auto) get() const&& noexcept {
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
