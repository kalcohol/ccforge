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

#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace std::execution::__forge_meta {

template<class CS>
struct value_tuple_list;

template<class List, class Sig>
struct push_value_tuple {
    using type = List;
};

template<class... Tuples, class... Vs>
struct push_value_tuple<type_list<Tuples...>, set_value_t(Vs...)> {
    using tuple_t = std::tuple<std::decay_t<Vs>...>;
    using type = list_push_unique_t<type_list<Tuples...>, tuple_t>;
};

template<class List, class... Sigs>
struct collect_value_tuples;

template<class List>
struct collect_value_tuples<List> {
    using type = List;
};

template<class List, class Sig, class... Rest>
struct collect_value_tuples<List, Sig, Rest...> {
    using next = typename push_value_tuple<List, Sig>::type;
    using type = typename collect_value_tuples<next, Rest...>::type;
};

template<class... Sigs>
struct value_tuple_list<completion_signatures<Sigs...>> {
    using type = typename collect_value_tuples<type_list<>, Sigs...>::type;
};

template<class CS>
using value_tuple_list_t = typename value_tuple_list<CS>::type;

template<class List>
struct type_list_empty;

template<class... Ts>
struct type_list_empty<type_list<Ts...>>
    : std::bool_constant<sizeof...(Ts) == 0> {};

template<class List>
inline constexpr bool type_list_empty_v = type_list_empty<List>::value;

template<class List>
struct single_value_or_variant_from_list;

template<>
struct single_value_or_variant_from_list<type_list<>> {
    using type = std::tuple<>;
};

template<class Tuple>
struct single_value_or_variant_from_list<type_list<Tuple>> {
    using type = Tuple;
};

template<class... Tuples>
struct single_value_or_variant_from_list<type_list<Tuples...>> {
    using type = std::variant<Tuples...>;
};

template<class List>
using single_value_or_variant_from_list_t =
    typename single_value_or_variant_from_list<List>::type;

template<class CS>
using single_value_or_variant_t =
    single_value_or_variant_from_list_t<value_tuple_list_t<CS>>;

template<class List>
struct value_variant_or_empty_tuple;

template<>
struct value_variant_or_empty_tuple<type_list<>> {
    using type = std::variant<std::tuple<>>;
};

template<class... Tuples>
struct value_variant_or_empty_tuple<type_list<Tuples...>> {
    using type = std::variant<Tuples...>;
};

template<class List>
using value_variant_or_empty_tuple_t =
    typename value_variant_or_empty_tuple<List>::type;

template<class Value, class Tuple>
Value value_from_tuple(Tuple&& tuple) {
    using tuple_t = std::remove_cvref_t<Tuple>;
    if constexpr (std::is_same_v<Value, tuple_t>) {
        return static_cast<Tuple&&>(tuple);
    } else {
        return Value{std::in_place_type<tuple_t>, static_cast<Tuple&&>(tuple)};
    }
}

} // namespace std::execution::__forge_meta

