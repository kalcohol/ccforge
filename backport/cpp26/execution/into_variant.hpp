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
#include "sync_wait.hpp"

#include <exception>
#include <optional>
#include <tuple>
#include <variant>

namespace std::execution {

namespace __forge_into_variant {

// Compute completion_signatures for into_variant:
// All set_value_t(Vs...) become set_value_t(variant<tuple<Vs>...>)
// error and stopped pass through unchanged.

template<class CS>
struct __iv_cs;

template<class... Sigs>
struct __iv_cs<completion_signatures<Sigs...>> {
private:
    // Collect all tuple types from set_value sigs
    template<class Sig>
    struct __is_value_sig : std::false_type {};
    template<class... Vs>
    struct __is_value_sig<set_value_t(Vs...)> : std::true_type {};

    template<class Sig>
    struct __tuple_of_sig { using type = void; };
    template<class... Vs>
    struct __tuple_of_sig<set_value_t(Vs...)> {
        using type = std::tuple<std::decay_t<Vs>...>;
    };

    // Build list of unique tuple types from value sigs
    template<class List>
    struct __list_to_variant_sig;

    template<bool Empty, class... Tuples>
    struct __variant_value_cs;

    template<class... Tuples>
    struct __variant_value_cs<true, Tuples...> {
        using type = completion_signatures<>;
    };

    template<class... Tuples>
    struct __variant_value_cs<false, Tuples...> {
        using type = completion_signatures<set_value_t(std::variant<Tuples...>)>;
    };

    template<class... Tuples>
    struct __list_to_variant_sig<__forge_meta::type_list<Tuples...>> {
        using type = typename __variant_value_cs<sizeof...(Tuples) == 0, Tuples...>::type;
    };

    template<class List, class Sig>
    struct __push_tuple_if_value {
        using type = List;
    };

    template<class... Ts, class... Vs>
    struct __push_tuple_if_value<__forge_meta::type_list<Ts...>, set_value_t(Vs...)> {
        using tup = std::tuple<std::decay_t<Vs>...>;
        using type = __forge_meta::list_push_unique_t<__forge_meta::type_list<Ts...>, tup>;
    };

    template<class List, class... Rest>
    struct __collect_tuples;
    template<class List>
    struct __collect_tuples<List> { using type = List; };
    template<class List, class Sig, class... Rest>
    struct __collect_tuples<List, Sig, Rest...> {
        using next = typename __push_tuple_if_value<List, Sig>::type;
        using type = typename __collect_tuples<next, Rest...>::type;
    };

    using __tuples = typename __collect_tuples<__forge_meta::type_list<>, Sigs...>::type;
    using __value_cs = typename __list_to_variant_sig<__tuples>::type;

    // Collect non-value sigs (error + stopped)
    template<class List, class Sig>
    struct __push_non_value {
        using type = __forge_meta::list_push_unique_t<List, Sig>;
    };
    template<class List, class... Vs>
    struct __push_non_value<List, set_value_t(Vs...)> {
        using type = List; // skip value sigs
    };

    template<class List, class... Rest>
    struct __collect_non_value;
    template<class List>
    struct __collect_non_value<List> { using type = List; };
    template<class List, class Sig, class... Rest>
    struct __collect_non_value<List, Sig, Rest...> {
        using next = typename __push_non_value<List, Sig>::type;
        using type = typename __collect_non_value<next, Rest...>::type;
    };

    template<class List>
    struct __list_to_cs;
    template<class... Ss>
    struct __list_to_cs<__forge_meta::type_list<Ss...>> {
        using type = completion_signatures<Ss...>;
    };

    using __non_value_list = typename __collect_non_value<__forge_meta::type_list<>, Sigs...>::type;
    using __non_value_cs = typename __list_to_cs<__non_value_list>::type;
    using __eptr_cs = completion_signatures<set_error_t(std::exception_ptr)>;

public:
    using type = __forge_meta::__concat_unique_cs_t<__value_cs, __non_value_cs, __eptr_cs>;
};

template<class CS>
using __iv_cs_t = typename __iv_cs<CS>::type;

// Compute the variant type from completion_signatures
template<class CS>
struct __variant_type_of;

template<class... Sigs>
struct __variant_type_of<completion_signatures<Sigs...>> {
    template<class Sig>
    struct __tuple_of { using type = void; };
    template<class... Vs>
    struct __tuple_of<set_value_t(Vs...)> {
        using type = std::tuple<std::decay_t<Vs>...>;
    };

    template<class List, class Sig>
    struct __push_value {
        using type = List;
    };
    template<class... Ts, class... Vs>
    struct __push_value<__forge_meta::type_list<Ts...>, set_value_t(Vs...)> {
        using tup = std::tuple<std::decay_t<Vs>...>;
        using type = __forge_meta::list_push_unique_t<__forge_meta::type_list<Ts...>, tup>;
    };

    template<class List, class... Rest>
    struct __collect;
    template<class List>
    struct __collect<List> { using type = List; };
    template<class List, class Sig, class... Rest>
    struct __collect<List, Sig, Rest...> {
        using next = typename __push_value<List, Sig>::type;
        using type = typename __collect<next, Rest...>::type;
    };

    template<class List>
    struct __to_variant;

    template<bool Empty, class... Ts>
    struct __variant_from_list;

    template<class... Ts>
    struct __variant_from_list<true, Ts...> {
        using type = std::variant<std::monostate>;
    };

    template<class... Ts>
    struct __variant_from_list<false, Ts...> {
        using type = std::variant<Ts...>;
    };

    template<class... Ts>
    struct __to_variant<__forge_meta::type_list<Ts...>> {
        using type = typename __variant_from_list<sizeof...(Ts) == 0, Ts...>::type;
    };

    using __tuples = typename __collect<__forge_meta::type_list<>, Sigs...>::type;
    using type = typename __to_variant<__tuples>::type;
};

template<class CS>
using __variant_type_of_t = typename __variant_type_of<CS>::type;

// Inner receiver: collects upstream values into the variant, then calls outer set_value
template<class R, class VariantT>
struct __into_variant_recv {
    using receiver_concept = receiver_t;
    R __rcvr;

    template<class... Vs>
    void set_value(Vs&&... vs) && noexcept {
        using tup_t = std::tuple<std::decay_t<Vs>...>;
        try {
            VariantT var(tup_t(static_cast<Vs&&>(vs)...));
            std::execution::set_value(std::move(__rcvr), std::move(var));
        } catch (...) {
            std::execution::set_error(std::move(__rcvr), std::current_exception());
        }
    }

    template<class E>
    void set_error(E&& e) && noexcept {
        std::execution::set_error(std::move(__rcvr), static_cast<E&&>(e));
    }

    void set_stopped() && noexcept {
        std::execution::set_stopped(std::move(__rcvr));
    }

    auto get_env() const noexcept -> env_of_t<R> {
        return std::execution::get_env(__rcvr);
    }
};

template<class S>
struct __into_variant_sender {
    using sender_concept = sender_t;
    using source_t = S;

    S __sndr;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept {
        using self_t = std::remove_cvref_t<Self>;
        using up_cs_t = decltype(std::execution::get_completion_signatures(
            std::declval<typename self_t::source_t>(),
            std::declval<Env>()));
        return __iv_cs_t<up_cs_t>{};
    }

    template<receiver R>
    auto connect(R r) && {
        using env_t = env_of_t<R>;
        using cs_t = decltype(std::execution::get_completion_signatures(
            std::declval<S>(), std::declval<env_t>()));
        using var_t = __variant_type_of_t<cs_t>;
        return std::execution::connect(
            std::move(__sndr),
            __into_variant_recv<R, var_t>{std::move(r)});
    }

    template<receiver R>
        requires std::copy_constructible<S>
    auto connect(R r) const& {
        using env_t = env_of_t<R>;
        using cs_t = decltype(std::execution::get_completion_signatures(
            std::declval<S>(), std::declval<env_t>()));
        using var_t = __variant_type_of_t<cs_t>;
        return std::execution::connect(
            S(__sndr),
            __into_variant_recv<R, var_t>{std::move(r)});
    }

    auto get_env() const noexcept -> env_of_t<S> {
        return std::execution::get_env(__sndr);
    }
};

} // namespace __forge_into_variant

namespace __forge_into_variant {

struct __into_variant_closure {
    template<sender S>
    [[nodiscard]] auto operator()(S&& sndr) const {
        return __into_variant_sender<std::decay_t<S>>{
            __forge_detail::__forward_as_given(std::forward<S>(sndr))};
    }

    template<sender S>
    friend auto operator|(S&& sndr, __into_variant_closure self) {
        return self(std::forward<S>(sndr));
    }
};

struct __into_variant_t {
    template<sender S>
    [[nodiscard]] auto operator()(S&& sndr) const {
        return __into_variant_sender<std::decay_t<S>>{
            __forge_detail::__forward_as_given(std::forward<S>(sndr))};
    }

    [[nodiscard]] auto operator()() const noexcept {
        return __into_variant_closure{};
    }
};

} // namespace __forge_into_variant

inline constexpr __forge_into_variant::__into_variant_t into_variant{};

} // namespace std::execution

namespace std::this_thread {

template<std::execution::sender S>
auto sync_wait_with_variant(S&& sndr) {
    auto result = std::this_thread::sync_wait(
        std::execution::into_variant(
            std::execution::__forge_detail::__forward_as_given(std::forward<S>(sndr))));
    using result_t = decltype(result);
    using tuple_t = typename result_t::value_type;
    using variant_t = std::remove_cvref_t<std::tuple_element_t<0, tuple_t>>;

    if (!result) {
        return std::optional<variant_t>{};
    }
    return std::optional<variant_t>{
        std::in_place, std::move(std::get<0>(*result))};
}

} // namespace std::this_thread

namespace std::execution {

using std::this_thread::sync_wait_with_variant;

} // namespace std::execution
