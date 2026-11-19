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

#include <cstddef>
#include <tuple>
#include <type_traits>

namespace std::execution::__forge_meta {

template<class List>
struct type_list_empty;

template<class... Ts>
struct type_list_empty<type_list<Ts...>>
    : std::bool_constant<sizeof...(Ts) == 0> {};

template<class List>
inline constexpr bool type_list_empty_v = type_list_empty<List>::value;

template<class Sig>
inline constexpr bool __is_value_signature = false;

template<class... Vs>
inline constexpr bool __is_value_signature<set_value_t(Vs...)> = true;

template<class CS>
inline constexpr std::size_t __value_count = 0;

template<class... Sigs>
inline constexpr std::size_t __value_count<completion_signatures<Sigs...>> =
    (std::size_t{0} + ... + __is_value_signature<Sigs>);

template<class CS>
    requires (__value_count<CS> == 1)
using __exact_value_tuple_t = __single_value_tuple_t<CS>;

} // namespace std::execution::__forge_meta
