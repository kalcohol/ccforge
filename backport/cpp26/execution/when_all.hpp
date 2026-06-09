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
#include "detail/value_result.hpp"
#include "detail/op_storage.hpp"
#include "env.hpp"
#include "stop_token.hpp"

#include <atomic>
#include <exception>
#include <mutex>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace std::execution {

namespace __forge_when_all {

struct __stopped_tag {};

template<class Env>
struct __child_env : std::decay_t<Env> {
    using base_t = std::decay_t<Env>;

    inplace_stop_token __token;

    __child_env(base_t env, inplace_stop_token token)
        : base_t(std::move(env)), __token(token) {}

    friend auto tag_invoke(get_stop_token_t, const __child_env& self) noexcept
        -> inplace_stop_token {
        return self.__token;
    }
};

template<class Env>
using __child_env_t = __child_env<std::decay_t<Env>>;

template<class CS>
using __value_tuple_list_t = __forge_meta::value_tuple_list_t<CS>;

template<class List>
using __type_list_empty = __forge_meta::type_list_empty<List>;

template<class... Lists>
struct __concat_type_lists;

template<>
struct __concat_type_lists<> {
    using type = __forge_meta::type_list<>;
};

template<class... Ts>
struct __concat_type_lists<__forge_meta::type_list<Ts...>> {
    using type = __forge_meta::type_list<Ts...>;
};

template<class... Ts, class... Us, class... Rest>
struct __concat_type_lists<__forge_meta::type_list<Ts...>, __forge_meta::type_list<Us...>, Rest...>
    : __concat_type_lists<__forge_meta::type_list<Ts..., Us...>, Rest...> {};

template<class... Lists>
using __concat_type_lists_t = typename __concat_type_lists<Lists...>::type;

template<class Prefix, class List>
struct __append_tuple_to_each;

template<class Prefix, class... Tuples>
struct __append_tuple_to_each<Prefix, __forge_meta::type_list<Tuples...>> {
    using type = __forge_meta::type_list<__forge_meta::__tuple_cat_t<Prefix, Tuples>...>;
};

template<class A, class B>
struct __tuple_product;

template<class... As, class... Bs>
struct __tuple_product<__forge_meta::type_list<As...>, __forge_meta::type_list<Bs...>> {
    using type = __concat_type_lists_t<
        typename __append_tuple_to_each<As, __forge_meta::type_list<Bs...>>::type...>;
};

template<class... Lists>
struct __cartesian_value_tuples;

template<>
struct __cartesian_value_tuples<> {
    using type = __forge_meta::type_list<std::tuple<>>;
};

template<class First, class... Rest>
struct __cartesian_value_tuples<First, Rest...> {
    using tail = typename __cartesian_value_tuples<Rest...>::type;
    using type = typename __tuple_product<First, tail>::type;
};

template<class Tuple>
struct __tuple_to_value_sig;

template<class... Vs>
struct __tuple_to_value_sig<std::tuple<Vs...>> {
    using type = set_value_t(Vs...);
};

template<class List>
struct __value_list_to_cs;

template<class... Tuples>
struct __value_list_to_cs<__forge_meta::type_list<Tuples...>> {
    using type = completion_signatures<typename __tuple_to_value_sig<Tuples>::type...>;
};

template<class... CSList>
using __cartesian_value_cs_t = typename __value_list_to_cs<
    typename __cartesian_value_tuples<__value_tuple_list_t<CSList>...>::type>::type;

template<class List, class Sig>
struct __push_error_type {
    using type = List;
};

template<class... Es, class E>
struct __push_error_type<__forge_meta::type_list<Es...>, set_error_t(E)> {
    using type = __forge_meta::list_push_unique_t<__forge_meta::type_list<Es...>, std::decay_t<E>>;
};

template<class List, class... Sigs>
struct __collect_error_types;

template<class List>
struct __collect_error_types<List> {
    using type = List;
};

template<class List, class Sig, class... Rest>
struct __collect_error_types<List, Sig, Rest...> {
    using next = typename __push_error_type<List, Sig>::type;
    using type = typename __collect_error_types<next, Rest...>::type;
};

template<class CS>
struct __error_type_list;

template<class... Sigs>
struct __error_type_list<completion_signatures<Sigs...>> {
    using type = typename __collect_error_types<__forge_meta::type_list<>, Sigs...>::type;
};

template<class List, class AddList>
struct __append_type_list_unique;

template<class List, class... Ts>
struct __append_type_list_unique<List, __forge_meta::type_list<Ts...>> {
    using type = __forge_meta::list_push_unique_all_t<List, Ts...>;
};

template<class List, class... Rest>
struct __concat_unique_type_lists;

template<class List>
struct __concat_unique_type_lists<List> {
    using type = List;
};

template<class List, class AddList, class... Rest>
struct __concat_unique_type_lists<List, AddList, Rest...> {
    using next = typename __append_type_list_unique<List, AddList>::type;
    using type = typename __concat_unique_type_lists<next, Rest...>::type;
};

template<class List>
struct __error_list_to_cs;

template<class... Es>
struct __error_list_to_cs<__forge_meta::type_list<Es...>> {
    using type = completion_signatures<set_error_t(Es)...>;
};

template<class... CSList>
using __error_type_list_t = typename __concat_unique_type_lists<
    __forge_meta::type_list<std::exception_ptr>, typename __error_type_list<CSList>::type...>::type;

template<class... CSList>
using __error_cs_t = typename __error_list_to_cs<__error_type_list_t<CSList...>>::type;

template<class List>
struct __error_variant_from_list;

template<class... Es>
struct __error_variant_from_list<__forge_meta::type_list<Es...>> {
    using type = std::variant<Es...>;
};

template<class S, class Env>
using __sender_value_variant_t = __forge_meta::value_variant_or_empty_tuple_t<
    __value_tuple_list_t<decltype(std::execution::get_completion_signatures(
        std::declval<S>(), std::declval<Env>()))>>;

template<class Env, class... Senders>
using __when_all_error_variant_t = typename __error_variant_from_list<__error_type_list_t<
    decltype(std::execution::get_completion_signatures(std::declval<Senders>(), std::declval<Env>()))...>>::type;

template<class OuterRecv, class... Senders>
struct __op;

template<std::size_t I, class OuterRecv, class... Senders>
struct __child_recv {
    using receiver_concept = receiver_t;
    __op<OuterRecv, Senders...>* __self;

    template<class... Vs>
    void set_value(Vs&&... vs) && noexcept {
        try {
            using tuple_t = std::tuple<std::decay_t<Vs>...>;
            std::get<I>(__self->__partial).emplace(
                std::in_place_type<tuple_t>, static_cast<Vs&&>(vs)...);
        } catch (...) {
            __self->child_fail(std::current_exception());
            return;
        }
        __self->child_done();
    }
    template<class E>
    void set_error(E&& e) && noexcept {
        __self->child_fail(static_cast<E&&>(e));
    }
    void set_stopped() && noexcept {
        __self->child_stop();
    }
    auto get_env() const noexcept {
        return __child_env_t<env_of_t<OuterRecv>>{
            std::execution::get_env(__self->__outer_recv),
            __self->__stop_src.get_token()};
    }
};

template<class OuterRecv, class... Senders>
struct __op : __forge_detail::__immovable {
    using operation_state_concept = operation_state_t;
    static constexpr std::size_t N = sizeof...(Senders);

    using child_env_t = __child_env_t<env_of_t<OuterRecv>>;
    using error_variant_t = __when_all_error_variant_t<child_env_t, Senders...>;

    template<class S>
    static constexpr bool __sender_has_value = !__type_list_empty<__value_tuple_list_t<
        decltype(std::execution::get_completion_signatures(
            std::declval<S>(), std::declval<child_env_t>()))>>::value;

    static constexpr bool __can_value_complete = (__sender_has_value<Senders> && ...);

    OuterRecv __outer_recv;
    inplace_stop_source __stop_src;
    std::atomic<std::size_t> __pending{N};
    std::atomic<bool> __failed{false};
    std::mutex __mtx;
    std::variant<std::monostate, error_variant_t, __stopped_tag> __result;
    std::tuple<std::optional<__sender_value_variant_t<Senders, child_env_t>>...> __partial;

    struct __outer_stop_callback {
        __op* __self;

        void operator()() const noexcept {
            __self->__stop_src.request_stop();
        }
    };

    __forge_detail::__op_storage<512> __child_storages[N];
    void* __child_ptrs[N]{};
    void (*__child_starts[N])(void*) noexcept;
    __forge_detail::__op_storage<256> __stop_callback_storage;

    template<std::size_t... Is>
    __op(OuterRecv r, std::tuple<Senders...> sndrs, std::index_sequence<Is...>)
        : __outer_recv(std::move(r))
    {
        (..., init_child<Is>(std::get<Is>(std::move(sndrs))));
    }

    template<std::size_t I>
    void init_child(std::tuple_element_t<I, std::tuple<Senders...>> sndr) {
        using S = std::tuple_element_t<I, std::tuple<Senders...>>;
        using child_op_t = connect_result_t<S, __child_recv<I, OuterRecv, Senders...>>;
        __child_ptrs[I] = __child_storages[I].template emplace_from<child_op_t>([&]() -> child_op_t {
            return std::execution::connect(
                std::move(sndr),
                __child_recv<I, OuterRecv, Senders...>{this});
        });
        __child_starts[I] = [](void* p) noexcept {
            std::execution::start(*static_cast<child_op_t*>(p));
        };
    }

    void register_outer_stop_callback() noexcept {
        using outer_env_t = env_of_t<OuterRecv>;

        if constexpr (requires(outer_env_t env) { std::execution::get_stop_token(env); }) {
            using outer_token_t = decltype(std::execution::get_stop_token(
                std::declval<outer_env_t>()));
            if constexpr (std::stoppable_token_for<outer_token_t, __outer_stop_callback>) {
                auto token = std::execution::get_stop_token(std::execution::get_env(__outer_recv));
                if (!token.stop_possible()) { return; }

                using callback_t = stop_callback_for_t<outer_token_t, __outer_stop_callback>;

                try {
                    __stop_callback_storage.template emplace<callback_t>(
                        token, __outer_stop_callback{this});
                } catch (...) {
                    __stop_src.request_stop();
                }
            }
        }
    }

    template<class E>
    void child_fail(E&& e) noexcept {
        bool expected = false;
        __failed.compare_exchange_strong(expected, true);
        try {
            std::lock_guard lk{__mtx};
            if (__result.index() != 1) {
                __result.template emplace<1>(
                    std::in_place_type<std::decay_t<E>>, static_cast<E&&>(e));
            }
        } catch (...) {
            std::lock_guard lk{__mtx};
            if (__result.index() != 1) {
                __result.template emplace<1>(
                    std::in_place_type<std::exception_ptr>, std::current_exception());
            }
        }
        __stop_src.request_stop();
        child_done();
    }

    void child_stop() noexcept {
        bool expected = false;
        if (__failed.compare_exchange_strong(expected, true)) {
            std::lock_guard lk{__mtx};
            if (__result.index() == 0) {
                __result.template emplace<2>(__stopped_tag{});
            }
        }
        __stop_src.request_stop();
        child_done();
    }

    void child_done() noexcept {
        if (--__pending == 0) deliver();
    }

    void deliver() noexcept {
        __stop_callback_storage.destroy();
        if (__result.index() == 1) {
            std::visit([this](auto& err) noexcept {
                std::execution::set_error(std::move(__outer_recv), std::move(err));
            }, std::get<1>(__result));
        } else if (__result.index() == 2) {
            std::execution::set_stopped(std::move(__outer_recv));
        } else {
            if constexpr (__can_value_complete) {
                try_deliver_values(std::index_sequence_for<Senders...>{});
            } else {
                std::execution::set_stopped(std::move(__outer_recv));
            }
        }
    }

    template<std::size_t... Is>
    void try_deliver_values(std::index_sequence<Is...>) noexcept {
        try {
            std::visit([this](auto&... values) noexcept {
                try {
                    auto combined = std::tuple_cat(std::move(values)...);
                    std::apply([&](auto&&... vs) {
                        std::execution::set_value(std::move(__outer_recv), std::move(vs)...);
                    }, std::move(combined));
                } catch (...) {
                    std::execution::set_error(std::move(__outer_recv), std::current_exception());
                }
            }, *std::get<Is>(__partial)...);
        } catch (...) {
            std::execution::set_error(std::move(__outer_recv), std::current_exception());
        }
    }

    void start() & noexcept {
        register_outer_stop_callback();
        constexpr std::size_t n = N;
        for (std::size_t i = 0; i < n; ++i)
            __child_starts[i](__child_ptrs[i]);
    }
};

template<class... Senders>
struct __sender {
    using sender_concept = sender_t;
    std::tuple<Senders...> __sndrs;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept {
        using child_env_t = __child_env_t<Env>;
        using cart_t = __cartesian_value_cs_t<
            decltype(std::execution::get_completion_signatures(
                std::declval<Senders>(), std::declval<child_env_t>()))...>;
        using err_cs = __error_cs_t<
            decltype(std::execution::get_completion_signatures(
                std::declval<Senders>(), std::declval<child_env_t>()))...>;
        static constexpr bool sends_stopped = (
            std::execution::__forge_meta::sends_stopped_from<
                decltype(std::execution::get_completion_signatures(
                    std::declval<Senders>(), std::declval<child_env_t>()))>::value || ...);
        using stp_cs = std::conditional_t<sends_stopped,
            completion_signatures<set_stopped_t()>,
            completion_signatures<>>;
        return std::execution::__forge_meta::__concat_unique_cs_t<cart_t, err_cs, stp_cs>{};
    }

    template<receiver R>
    auto connect(R r) && {
        return __op<R, Senders...>{
            std::move(r),
            std::move(__sndrs),
            std::index_sequence_for<Senders...>{}};
    }

    template<receiver R>
        requires (std::copy_constructible<Senders> && ...)
    auto connect(R r) const& {
        return __op<R, Senders...>{
            std::move(r),
            __sndrs,
            std::index_sequence_for<Senders...>{}};
    }

    auto get_env() const noexcept -> empty_env {
        return {};
    }
};

} // namespace __forge_when_all

template<sender... Senders>
[[nodiscard]] auto when_all(Senders&&... sndrs) {
    return __forge_when_all::__sender<std::decay_t<Senders>...>{
        std::tuple<std::decay_t<Senders>...>{
            __forge_detail::__forward_as_given(std::forward<Senders>(sndrs))...}};
}

template<sender... Senders>
    requires (sizeof...(Senders) > 0)
[[nodiscard]] auto when_all_with_variant(Senders&&... sndrs) {
    return std::execution::when_all(
        std::execution::into_variant(
            __forge_detail::__forward_as_given(std::forward<Senders>(sndrs)))...);
}

} // namespace std::execution
