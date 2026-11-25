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

// Protocol model: formal/when_all; every edit, comments included, requires a source-lock refresh.

#include "concepts.hpp"
#include "detail/adaptor.hpp"
#include "detail/value_result.hpp"
#include "detail/op_holder.hpp"
#include "env.hpp"
#include "into_variant.hpp"
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
struct __no_error {};

template<class Env>
struct __child_env {
    const Env* __base;
    inplace_stop_token __token;

    auto query(get_stop_token_t) const noexcept -> inplace_stop_token {
        return this->__token;
    }

    template<class Query>
        requires (!std::same_as<std::remove_cvref_t<Query>, get_stop_token_t> &&
                  __forge_env_detail::__forwarding_query_type<std::remove_cvref_t<Query>> &&
                  __forge_env_detail::__raw_queryable<Query, Env>)
    decltype(auto) query(Query&& query) const
        noexcept(__forge_env_detail::__nothrow_raw_queryable<Query, Env>) {
        return __base->query(std::forward<Query>(query));
    }
};

template<class Env>
using __child_env_t = __child_env<__forge_env_detail::__normalized_env_t<Env>>;

template<class Child, class Env>
using __child_cs_t = completion_signatures_of_t<Child, __child_env_t<Env>>;

template<class Env, class... Children>
concept __valid_children =
    (sender_in<Children, __child_env_t<Env>> && ...) &&
    ((__forge_meta::__value_count<__child_cs_t<Children, Env>> < 2) && ...) &&
    (__forge_detail::__decay_copyable_cs<__child_cs_t<Children, Env>>::value && ...);

template<class Child, class Env>
using __fwd_cs_t = completion_signatures_of_t<Child, __forge_detail::__fwd_env_t<Env>>;

template<class Env, class... Children>
struct __value_storage {
    using type = std::tuple<>;
};

template<class Env, class... Children>
    requires requires { typename std::tuple<__forge_meta::__exact_value_tuple_t<
        __fwd_cs_t<Children, Env>>...>; }
struct __value_storage<Env, Children...> {
    using type = std::tuple<std::optional<
        __forge_meta::__exact_value_tuple_t<__fwd_cs_t<Children, Env>>>...>;
};

template<class Env, class... Children>
using __values_tuple_t = typename __value_storage<Env, Children...>::type;

template<class Tuple>
struct __tuple_to_value_sig;

template<class... Vs>
struct __tuple_to_value_sig<std::tuple<Vs...>> {
    using type = set_value_t(Vs...);
};

template<class Values>
struct __value_signature;

template<class... Tuples>
struct __value_signature<std::tuple<std::optional<Tuples>...>> {
    using type = typename __tuple_to_value_sig<__forge_meta::__tuple_cat_t<Tuples...>>::type;
};

template<class Env, class... Children>
inline constexpr bool __has_stopped_completion =
    (__forge_meta::sends_stopped_from<__child_cs_t<Children, Env>>::value || ...);


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

template<class Sig>
struct __result_copy_may_throw : std::false_type {};

template<class... Vs>
struct __result_copy_may_throw<set_value_t(Vs...)>
    : std::bool_constant<!std::is_nothrow_constructible_v<
          std::tuple<std::decay_t<Vs>...>, Vs...>> {};

template<class E>
struct __result_copy_may_throw<set_error_t(E)>
    : std::bool_constant<
          !std::is_nothrow_constructible_v<std::decay_t<E>, E>> {};

template<class CS>
struct __completion_copy_may_throw;

template<class... Sigs>
struct __completion_copy_may_throw<completion_signatures<Sigs...>>
    : std::bool_constant<(__result_copy_may_throw<Sigs>::value || ...)> {};

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
    std::conditional_t<
        (__completion_copy_may_throw<CSList>::value || ...),
        __forge_meta::type_list<std::exception_ptr>,
        __forge_meta::type_list<>>,
    typename __error_type_list<CSList>::type...>::type;

template<class... CSList>
using __error_cs_t = typename __error_list_to_cs<__error_type_list_t<CSList...>>::type;

template<class List>
struct __error_variant_from_list;

template<class... Es>
struct __error_variant_from_list<__forge_meta::type_list<Es...>> {
    using type = std::variant<__no_error, Es...>;
};

template<class OuterRecv, class... Senders>
struct __op;

template<class Token, class Callback,
         bool = std::__forge_stop_detail::__stoppable_token_for<Token, Callback>>
struct __callback_type {
    using type = std::monostate;
};

template<class Token, class Callback>
struct __callback_type<Token, Callback, true> {
    using type = stop_callback_for_t<Token, Callback>;
};

template<std::size_t I, class OuterRecv, class... Senders>
struct __child_recv {
    using receiver_concept = receiver_tag;
    __op<OuterRecv, Senders...>* __self;
    // A child can query its environment while its connection type is deduced.
    __child_env_t<env_of_t<OuterRecv>> __env;

    template<class... Vs>
    void set_value(Vs&&... vs) && noexcept {
        using op_t = __op<OuterRecv, Senders...>;
        if constexpr (!std::same_as<typename op_t::values_tuple, std::tuple<>>) {
            if (__self->__disp == op_t::__disposition::started) {
                auto& slot = std::get<I>(__self->__values);
                if constexpr (std::is_nothrow_constructible_v<std::tuple<std::decay_t<Vs>...>, Vs...>) {
                    slot.emplace(std::forward<Vs>(vs)...);
                } else {
                    try {
                        slot.emplace(std::forward<Vs>(vs)...);
                    } catch (...) {
                        __self->child_fail(std::current_exception());
                        return;
                    }
                }
            }
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
    auto get_env() const noexcept -> __child_env_t<env_of_t<OuterRecv>> {
        return __env;
    }
};

// Form the exact child operations without instantiating the enclosing operation.
// The index pack is the only pack of the function template: MSVC rejects explicit
// arguments for one pack followed by a deduced pack expanded in the same pattern.
template<class R, class... Children>
struct __child_ops {
    template<std::size_t... Is>
    static auto __type(std::index_sequence<Is...>)
        -> std::tuple<__forge_detail::__op_holder<
            connect_result_t<Children, __child_recv<Is, R, Children...>>>...>;
};

template<class R, class... Children>
using __child_ops_t = decltype(__child_ops<R, Children...>::__type(
    std::index_sequence_for<Children...>{}));

template<class R, class... Children>
concept __connectable_children = __valid_children<env_of_t<R>, Children...> &&
    requires { typename __child_ops_t<R, Children...>; };

template<class OuterRecv, class... Senders>
struct __op : __forge_detail::__immovable {
    using operation_state_concept = operation_state_tag;
    static constexpr std::size_t N = sizeof...(Senders);

    using outer_env_t = env_of_t<OuterRecv>;
    using values_tuple = __values_tuple_t<outer_env_t, Senders...>;
    using error_variant_t = typename __error_variant_from_list<
        __error_type_list_t<__child_cs_t<Senders, outer_env_t>...>>::type;

    static constexpr bool __sends_stopped = __has_stopped_completion<outer_env_t, Senders...>;

    enum class __disposition {
        started,
        error,
        stopped
    };

    OuterRecv __outer_recv;
    __forge_env_detail::__receiver_env<OuterRecv> __outer_env;
    inplace_stop_source __stop_src;
    std::atomic<std::size_t> __pending{N};
    std::atomic<__disposition> __disp{__disposition::started};
    std::mutex __mtx;
    std::variant<std::monostate, error_variant_t, __stopped_tag> __result;
    values_tuple __values;

    struct __outer_stop_callback {
        __op* __self;

        void operator()() const noexcept {
            auto* self = __self;
            // Take a completion credit only while children are still pending.
            // A terminal thread may already be waiting in callback deregistration.
            auto pending = self->__pending.load(std::memory_order_acquire);
            while (pending != 0) {
                if (self->__pending.compare_exchange_weak(
                        pending, pending + 1, std::memory_order_acq_rel)) {
                    self->__stop_src.request_stop();
                    self->child_done();
                    return;
                }
            }
        }
    };

    using __outer_token_t = std::remove_cvref_t<decltype(
        std::get_stop_token(std::declval<env_of_t<OuterRecv>>()))>;
    using __callback_t = typename __callback_type<__outer_token_t, __outer_stop_callback>::type;

    __child_ops_t<OuterRecv, Senders...> __children;
    // Unregister before destroying children, including an unstarted operation.
    std::optional<__callback_t> __stop_callback;

    template<class Tuple, std::size_t... Is>
    __op(OuterRecv&& r, Tuple&& sndrs, std::index_sequence<Is...>)
        : __outer_recv(std::move(r))
        , __outer_env(__outer_recv)
        , __children([&]() {
            return std::execution::connect(std::get<Is>(std::forward<Tuple>(sndrs)),
                __child_recv<Is, OuterRecv, Senders...>{this,
                    {std::addressof(__outer_env.__get()), __stop_src.get_token()}});
        }...)
    {}

    void register_outer_stop_callback() noexcept {
        using outer_env_t = env_of_t<OuterRecv>;

        if constexpr (requires(outer_env_t env) { std::get_stop_token(env); }) {
            using outer_token_t = std::remove_cvref_t<decltype(
                std::get_stop_token(std::declval<outer_env_t>()))>;
            if constexpr (std::__forge_stop_detail::__stoppable_token_for<outer_token_t, __outer_stop_callback>) {
                auto token = std::get_stop_token(std::execution::get_env(__outer_recv));
                if (!token.stop_possible()) { return; }

                try {
                    __stop_callback.emplace(token, __outer_stop_callback{this});
                } catch (...) {
                    __stop_src.request_stop();
                }
            }
        }
    }

    template<class E>
    void child_fail(E&& e) noexcept {
        if (__disp.exchange(__disposition::error) != __disposition::error) {
            __stop_src.request_stop();
            if constexpr (std::is_nothrow_constructible_v<std::decay_t<E>, E>) {
                std::lock_guard lk{__mtx};
                __result.template emplace<1>(
                    std::in_place_type<std::decay_t<E>>, static_cast<E&&>(e));
            } else {
                try {
                    std::lock_guard lk{__mtx};
                    __result.template emplace<1>(
                        std::in_place_type<std::decay_t<E>>, static_cast<E&&>(e));
                } catch (...) {
                    std::lock_guard lk{__mtx};
                    __result.template emplace<1>(
                        std::in_place_type<std::exception_ptr>,
                        std::current_exception());
                }
            }
        }
        child_done();
    }

    void child_stop() noexcept {
        auto expected = __disposition::started;
        if (__disp.compare_exchange_strong(
                expected, __disposition::stopped)) {
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
        __stop_callback.reset();
        if (__result.index() == 1) {
            std::visit([this](auto& err) noexcept {
                using error_t = std::remove_cvref_t<decltype(err)>;
                if constexpr (std::same_as<error_t, __no_error>) {
                    std::terminate();
                } else {
                    std::execution::set_error(
                        std::move(__outer_recv), std::move(err));
                }
            }, std::get<1>(__result));
        } else if (__result.index() == 2) {
            if constexpr (__sends_stopped) {
                std::execution::set_stopped(std::move(__outer_recv));
            } else {
                std::terminate();
            }
        } else {
            deliver_values();
        }
    }

    template<class... Ts>
    static auto __tie_values(std::tuple<Ts...>& values) noexcept {
        return std::apply([](auto&... values) noexcept { return std::tie(values...); }, values);
    }

    void deliver_values() noexcept {
        auto set = [this](auto&... values) noexcept {
            std::execution::set_value(std::move(__outer_recv), std::move(values)...);
        };
        std::apply([&](auto&... slots) noexcept {
            std::apply(set, std::tuple_cat(__tie_values(*slots)...));
        }, __values);
    }

    template<std::size_t... Is>
    void __start_children(std::index_sequence<Is...>) noexcept {
        (std::execution::start(std::get<Is>(__children).__op), ...);
    }

    void start() & noexcept {
        register_outer_stop_callback();
        __start_children(std::index_sequence_for<Senders...>{});
    }
};

template<class... Senders>
struct __sender {
    using sender_concept = sender_tag;
    std::tuple<Senders...> __sndrs;

    template<class Env>
        requires __valid_children<Env, Senders&...>
    constexpr auto get_completion_signatures(Env&&) & noexcept {
        return __signatures<Env, Senders&...>();
    }

    template<class Env>
        requires __valid_children<Env, Senders&&...>
    constexpr auto get_completion_signatures(Env&&) && noexcept {
        return __signatures<Env, Senders&&...>();
    }

    template<class Env>
        requires __valid_children<Env, const Senders&...>
    constexpr auto get_completion_signatures(Env&&) const& noexcept {
        return __signatures<Env, const Senders&...>();
    }

    template<class Env>
        requires __valid_children<Env, const Senders&&...>
    constexpr auto get_completion_signatures(Env&&) const&& noexcept {
        return __signatures<Env, const Senders&&...>();
    }

    // A rejected child category must not retry a const-lvalue query or connection.
    template<class Env> void get_completion_signatures(Env&&) & = delete;
    template<class Env> void get_completion_signatures(Env&&) && = delete;
    template<class Env> void get_completion_signatures(Env&&) const&& = delete;

    template<receiver R>
        requires __connectable_children<R, Senders&...>
    auto connect(R r) & {
        return __connect(std::move(r), __sndrs, std::index_sequence_for<Senders...>{});
    }

    template<receiver R>
        requires __connectable_children<R, Senders&&...>
    auto connect(R r) && {
        return __connect(std::move(r), std::move(__sndrs), std::index_sequence_for<Senders...>{});
    }

    template<receiver R>
        requires __connectable_children<R, const Senders&...>
    auto connect(R r) const& {
        return __connect(std::move(r), __sndrs, std::index_sequence_for<Senders...>{});
    }

    template<receiver R>
        requires __connectable_children<R, const Senders&&...>
    auto connect(R r) const&& {
        return __connect(std::move(r), std::move(__sndrs), std::index_sequence_for<Senders...>{});
    }

    template<class R> void connect(R) & = delete;
    template<class R> void connect(R) && = delete;
    template<class R> void connect(R) const&& = delete;

    auto get_env() const noexcept -> __empty_env {
        return {};
    }

private:
    template<class R, class Tuple, std::size_t... Is>
    static auto __connect(R&& r, Tuple&& sndrs, std::index_sequence<Is...> indices) {
        return __op<R, decltype(std::get<Is>(std::forward<Tuple>(sndrs)))...>{
            std::move(r), std::forward<Tuple>(sndrs), indices};
    }

    template<class Env, class... Children>
    static constexpr auto __signatures() noexcept {
        using values_tuple = __values_tuple_t<Env, Children...>;
        using value_cs = completion_signatures<typename __value_signature<values_tuple>::type>;
        using error_cs = __error_cs_t<__child_cs_t<Children, Env>...>;
        using stopped_cs = std::conditional_t<__has_stopped_completion<Env, Children...>,
            completion_signatures<set_stopped_t()>, completion_signatures<>>;
        return __forge_meta::__concat_unique_cs_t<value_cs, error_cs, stopped_cs>{};
    }
};

} // namespace __forge_when_all

struct when_all_t {
    template<sender... Senders>
        requires (sizeof...(Senders) > 0)
    [[nodiscard]] auto operator()(Senders&&... sndrs) const {
        return __forge_when_all::__sender<std::decay_t<Senders>...>{
            std::tuple<std::decay_t<Senders>...>{
                __forge_detail::__forward_as_given(
                    std::forward<Senders>(sndrs))...}};
    }
};

inline constexpr when_all_t when_all{};

struct when_all_with_variant_t {
    template<sender... Senders>
        requires (sizeof...(Senders) > 0)
    [[nodiscard]] auto operator()(Senders&&... sndrs) const {
        return std::execution::when_all(
            std::execution::into_variant(
                __forge_detail::__forward_as_given(
                    std::forward<Senders>(sndrs)))...);
    }
};

inline constexpr when_all_with_variant_t when_all_with_variant{};

} // namespace std::execution
