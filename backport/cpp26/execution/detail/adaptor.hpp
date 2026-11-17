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

#include "../concepts.hpp"
#include "../env.hpp"

#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

namespace std::execution::__forge_detail {

template<class T>
concept __movable_value =
    std::move_constructible<std::decay_t<T>> &&
    std::constructible_from<std::decay_t<T>, T> &&
    !std::is_array_v<std::remove_reference_t<T>>;

// These senders expose an rvalue connection and a copyable const-lvalue
// connection. Query the same child category selected by those overloads.
template<class Self, class Child>
using __child_category_t = std::conditional_t<
    std::is_rvalue_reference_v<Self&&> &&
    !std::is_const_v<std::remove_reference_t<Self>>, Child, const Child&>;

template<class Env>
using __fwd_env_t = __forge_env_detail::__forwarding_attrs<Env>;

template<class Fn, class Tag, class Sig>
struct __accepts_completion : std::true_type {};

template<class Fn, class Tag, class... As>
struct __accepts_completion<Fn, Tag, Tag(As...)>
    : std::bool_constant<std::is_invocable_v<Fn, As...>> {};

template<class Fn, class Tag, class CS>
struct __accepts_completions : std::false_type {};

template<class Fn, class Tag, class... Sigs>
struct __accepts_completions<Fn, Tag, completion_signatures<Sigs...>>
    : std::bool_constant<(__accepts_completion<Fn, Tag, Sigs>::value && ...)> {};

template<class Sig>
struct __decay_copyable_sig;

template<class Tag, class... As>
struct __decay_copyable_sig<Tag(As...)>
    : std::bool_constant<(std::is_constructible_v<std::decay_t<As>, As> && ...)> {};

template<class CS>
struct __decay_copyable_cs : std::false_type {};

template<class... Sigs>
struct __decay_copyable_cs<completion_signatures<Sigs...>>
    : std::bool_constant<(__decay_copyable_sig<Sigs>::value && ...)> {};

// With no allocator preserve the original category; otherwise reconstruct
// the data using uses-allocator construction inside the noexcept get-state
// boundary required by [exec.snd.expos]/37, including reconstruction failures.
template<class T, class R>
constexpr decltype(auto) __allocator_aware_forward(T&& obj, const R& rcvr)
    noexcept {
    if constexpr (requires { std::get_allocator(std::execution::get_env(rcvr)); }) {
        auto alloc = std::get_allocator(std::execution::get_env(rcvr));
        return std::make_obj_using_allocator<std::remove_cvref_t<T>>(
            alloc, static_cast<T&&>(obj));
    } else {
        return static_cast<T&&>(obj);
    }
}

// The value tuple represents exposition-only product data, whose elements
// receive the allocator independently (unlike an arbitrary user tuple).
template<class Tuple, class R, std::size_t... Is>
constexpr decltype(auto) __allocator_aware_product(Tuple&& obj, const R& rcvr,
                                                 std::index_sequence<Is...>) noexcept {
    if constexpr (requires { std::get_allocator(std::execution::get_env(rcvr)); }) {
        return std::remove_cvref_t<Tuple>{
            __allocator_aware_forward(std::get<Is>(static_cast<Tuple&&>(obj)), rcvr)...};
    } else {
        return static_cast<Tuple&&>(obj);
    }
}

} // namespace std::execution::__forge_detail
