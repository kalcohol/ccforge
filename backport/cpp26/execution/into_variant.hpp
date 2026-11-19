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

#include "sender_adaptor_closure.hpp"
#include "concepts.hpp"
#include "env.hpp"
#include "detail/adaptor.hpp"
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
struct __iv_cs {};

template<class... Sigs>
    requires __forge_detail::__decay_copyable_cs<completion_signatures<Sigs...>>::value
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

    template<class Sig>
    struct __value_may_throw : std::false_type {};

    template<class... Vs>
    struct __value_may_throw<set_value_t(Vs...)>
        : std::bool_constant<!std::is_nothrow_constructible_v<
              std::tuple<std::decay_t<Vs>...>, Vs...>> {};

    static constexpr bool __may_throw =
        (__value_may_throw<Sigs>::value || ...);
    using __eptr_cs = std::conditional_t<
        __may_throw,
        completion_signatures<set_error_t(std::exception_ptr)>,
        completion_signatures<>>;

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
        using type = __forge_meta::__empty_variant;
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
    using receiver_concept = receiver_tag;
    R* __rcvr;
    // The environment that the operation state keeps for this receiver.
    const __forge_env_detail::__receiver_env<R>* __env = nullptr;

    template<class... Vs>
    void set_value(Vs&&... vs) && noexcept {
        using tup_t = std::tuple<std::decay_t<Vs>...>;
        constexpr bool nothrow =
            std::is_nothrow_constructible_v<tup_t, Vs...>;
        if constexpr (nothrow) {
            VariantT var(
                std::in_place_type<tup_t>, static_cast<Vs&&>(vs)...);
            std::execution::set_value(std::move(*__rcvr), std::move(var));
        } else {
            try {
                VariantT var(
                    std::in_place_type<tup_t>, static_cast<Vs&&>(vs)...);
                std::execution::set_value(std::move(*__rcvr), std::move(var));
            } catch (...) {
                std::execution::set_error(
                    std::move(*__rcvr), std::current_exception());
            }
        }
    }

    template<class E>
    void set_error(E&& e) && noexcept {
        std::execution::set_error(std::move(*__rcvr), static_cast<E&&>(e));
    }

    void set_stopped() && noexcept {
        std::execution::set_stopped(std::move(*__rcvr));
    }

    auto get_env() const noexcept -> __forge_detail::__fwd_env_t<env_of_t<R>> {
        return __env->__view();
    }
};

template<class S>
struct __into_variant_sender {
    using sender_concept = sender_tag;
    using source_t = S;

    S __sndr;

    template<class Child, class Env>
    using __cs = __iv_cs_t<completion_signatures_of_t<Child, __forge_detail::__fwd_env_t<Env>>>;
    template<class Child, class R>
    using __recv = __into_variant_recv<R, __variant_type_of_t<
        completion_signatures_of_t<Child, __forge_detail::__fwd_env_t<env_of_t<R>>>>>;

    template<class Self, class Env>
    static constexpr auto get_completion_signatures() noexcept
        -> __cs<__forge_detail::__child_category_t<Self, S>, Env> { return {}; }

    template<class Child, class R>
    using __op = __forge_detail::__receiver_op<Child, R, __recv<Child, R>>;

    template<receiver R>
        requires requires { typename __cs<S, env_of_t<R>>;
                     typename connect_result_t<S, __recv<S, R>>; }
    auto connect(R r) &&
        noexcept(noexcept(__op<S, R>(std::move(__sndr), std::move(r)))) -> __op<S, R> {
        return __op<S, R>(std::move(__sndr), std::move(r));
    }

    template<receiver R>
        requires std::copy_constructible<S> && requires { typename __cs<const S&, env_of_t<R>>;
                     typename connect_result_t<const S&, __recv<const S&, R>>; }
    auto connect(R r) const&
        noexcept(noexcept(__op<const S&, R>(__sndr, std::move(r)))) -> __op<const S&, R> {
        return __op<const S&, R>(__sndr, std::move(r));
    }

    // An invalid rvalue child connection must not retry the const-lvalue path.
    template<class R> void connect(R) && = delete;

    auto get_env() const noexcept {
        return __forge_env_detail::__fwd_attrs_view<env_of_t<S>>::__of(__sndr);
    }
};

} // namespace __forge_into_variant

namespace __forge_into_variant {

struct __into_variant_t : sender_adaptor_closure<__into_variant_t> {
    template<sender S>
    [[nodiscard]] auto operator()(S&& sndr) const {
        return __into_variant_sender<std::decay_t<S>>{
            __forge_detail::__forward_as_given(std::forward<S>(sndr))};
    }
};

} // namespace __forge_into_variant

using into_variant_t = __forge_into_variant::__into_variant_t;
inline constexpr into_variant_t into_variant{};

} // namespace std::execution

namespace std::this_thread {

struct sync_wait_with_variant_t {
private:
    using __env_t = std::execution::__forge_sync_wait::sync_wait_env;

    template<class S>
    using __adapted_t = decltype(std::execution::into_variant(std::declval<S>()));

    template<class S>
    using __result_t = std::optional<std::execution::value_types_of_t<S, __env_t>>;

public:
    template<class S>
    auto operator()(S&& sndr) const {
        namespace ex = std::execution;
        using adapted_t = __adapted_t<S>;
        if constexpr (!ex::sender_in<adapted_t, __env_t>) {
            static_assert(ex::sender_in<adapted_t, __env_t>,
                          "sync_wait_with_variant requires an adapted sender in its run_loop environment");
        } else {
            constexpr bool has_result = requires { typename __result_t<adapted_t>; };
            static_assert(has_result, "sync_wait_with_variant requires a well-formed result type");
            if constexpr (has_result) {
                using domain_t = ex::__forge_domain::__compl_domain_t<ex::set_value_t, S, __env_t>;
                using applied_t = decltype(ex::apply_sender(
                    domain_t{}, *this, std::forward<S>(sndr)));
                static_assert(std::same_as<applied_t, __result_t<adapted_t>>,
                              "sync_wait_with_variant domain must return its prescribed optional variant");
                return ex::apply_sender(domain_t{}, *this, std::forward<S>(sndr));
            }
        }
    }

    template<class S>
    auto apply_sender(S&& sndr) const -> __result_t<__adapted_t<S>> {
        using result_t = __result_t<__adapted_t<S>>;
        if (auto value = std::this_thread::sync_wait(
                std::execution::into_variant(std::forward<S>(sndr)))) {
            return result_t(std::move(std::get<0>(*value)));
        }
        return result_t(std::nullopt);
    }
};

inline constexpr sync_wait_with_variant_t sync_wait_with_variant{};

} // namespace std::this_thread
