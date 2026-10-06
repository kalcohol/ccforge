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

#include <exception>
#include <memory>

namespace std::execution {

namespace __forge_then {

template<class... Ts>
struct sig_list {};

template<class List, class T>
struct sig_list_contains;

template<class... Ts, class T>
struct sig_list_contains<sig_list<Ts...>, T> : std::bool_constant<(std::is_same_v<T, Ts> || ...)> {};

template<class List, class T>
inline constexpr bool sig_list_contains_v = sig_list_contains<List, T>::value;

template<class List, class T>
struct sig_list_push_unique;

template<class... Ts, class T>
struct sig_list_push_unique<sig_list<Ts...>, T> {
    using type = std::conditional_t<sig_list_contains_v<sig_list<Ts...>, T>, sig_list<Ts...>, sig_list<Ts..., T>>;
};

template<class List, class T>
using sig_list_push_unique_t = typename sig_list_push_unique<List, T>::type;

template<class List>
struct sig_list_to_completion_signatures;

template<class... Ts>
struct sig_list_to_completion_signatures<sig_list<Ts...>> {
    using type = completion_signatures<Ts...>;
};

template<class List>
using sig_list_to_completion_signatures_t = typename sig_list_to_completion_signatures<List>::type;

template<class R>
struct value_sig_from_result {
    using type = set_value_t(R);
};
template<>
struct value_sig_from_result<void> {
    using type = set_value_t();
};

template<class Fn, class UpSig>
struct transform_sig {
    using type = UpSig;
    static constexpr bool may_throw = false;
};

template<class Fn, class... Vs>
struct transform_sig<Fn, set_value_t(Vs...)> {
    using r_t = std::invoke_result_t<Fn, Vs...>;
    using type = typename value_sig_from_result<r_t>::type;
    static constexpr bool may_throw =
        !std::is_nothrow_invocable_v<Fn, Vs...>;
};

template<class Fn, class E>
struct transform_sig<Fn, set_error_t(E)> {
    using type = set_error_t(E);
    static constexpr bool may_throw = false;
};

template<class Fn>
struct transform_sig<Fn, set_stopped_t()> {
    using type = set_stopped_t();
    static constexpr bool may_throw = false;
};

template<class Fn, class UpCompletionSignatures>
struct transform_completion_signatures;

template<class Fn, class... UpSigs>
struct transform_completion_signatures<Fn, completion_signatures<UpSigs...>> {
private:
    template<class List, class Sig>
    struct add_one {
        using transformed = typename transform_sig<Fn, Sig>::type;
        using type = sig_list_push_unique_t<List, transformed>;
    };

    template<class List, class... Rest>
    struct add_all;

    template<class List>
    struct add_all<List> {
        using type = List;
    };

    template<class List, class Sig, class... Rest>
    struct add_all<List, Sig, Rest...> {
        using next = typename add_one<List, Sig>::type;
        using type = typename add_all<next, Rest...>::type;
    };

    using initial = sig_list<>;
    using mapped = typename add_all<initial, UpSigs...>::type;
    static constexpr bool may_throw =
        (transform_sig<Fn, UpSigs>::may_throw || ...);
    using with_eptr = std::conditional_t<
        may_throw,
        sig_list_push_unique_t<mapped, set_error_t(std::exception_ptr)>,
        mapped>;

public:
    using type = sig_list_to_completion_signatures_t<with_eptr>;
};

template<class Fn, class UpCompletionSignatures>
using transform_completion_signatures_t = typename transform_completion_signatures<Fn, UpCompletionSignatures>::type;

template<class R, class Fn>
struct then_receiver {
    using receiver_concept = receiver_t;

    R* rcvr_;
    Fn* fn_;

    template<class... Vs>
    void set_value(Vs&&... vs) && noexcept {
        if constexpr (std::is_nothrow_invocable_v<Fn, Vs...>) {
            if constexpr (std::is_void_v<std::invoke_result_t<Fn, Vs...>>) {
                std::invoke(std::move(*fn_), static_cast<Vs&&>(vs)...);
                std::execution::set_value(std::move(*rcvr_));
            } else {
                std::execution::set_value(
                    std::move(*rcvr_),
                    std::invoke(std::move(*fn_), static_cast<Vs&&>(vs)...));
            }
        } else if constexpr (std::is_void_v<std::invoke_result_t<Fn, Vs...>>) {
            try {
                std::invoke(std::move(*fn_), static_cast<Vs&&>(vs)...);
                std::execution::set_value(std::move(*rcvr_));
            } catch (...) {
                std::execution::set_error(std::move(*rcvr_), std::current_exception());
            }
        } else {
            try {
                std::execution::set_value(
                    std::move(*rcvr_),
                    std::invoke(std::move(*fn_), static_cast<Vs&&>(vs)...));
            } catch (...) {
                std::execution::set_error(std::move(*rcvr_), std::current_exception());
            }
        }
    }

    template<class E>
    void set_error(E&& e) && noexcept {
        std::execution::set_error(std::move(*rcvr_), static_cast<E&&>(e));
    }

    void set_stopped() && noexcept {
        std::execution::set_stopped(std::move(*rcvr_));
    }

    auto get_env() const noexcept -> env_of_t<R> {
        return std::execution::get_env(*rcvr_);
    }
};

template<class S, class Fn, class R>
struct then_op : __forge_detail::__immovable {
    using operation_state_concept = operation_state_t;
    using inner_op_t = connect_result_t<S, then_receiver<R, Fn>>;

    [[no_unique_address]] R rcvr_;
    [[no_unique_address]] Fn fn_;
    // Destroy the inner operation before the state borrowed by its receiver.
    inner_op_t inner_;

    then_op(S sndr, Fn fn, R rcvr)
        : rcvr_(std::move(rcvr))
        , fn_(std::move(fn))
        , inner_(std::execution::connect(
            std::move(sndr), then_receiver<R, Fn>{std::addressof(rcvr_), std::addressof(fn_)}))
    {}

    void start() & noexcept {
        std::execution::start(inner_);
    }
};

template<class S, class Fn>
struct then_sender {
    using sender_concept = sender_t;
    using source_t = S;
    using fn_t = Fn;

    [[no_unique_address]] S sndr_;
    [[no_unique_address]] Fn fn_;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept {
        using self_t = std::remove_cvref_t<Self>;
        using up_cs_t = decltype(std::execution::get_completion_signatures(
            std::declval<typename self_t::source_t>(),
            std::declval<Env>()));
        using out_cs_t = transform_completion_signatures_t<typename self_t::fn_t, up_cs_t>;
        return out_cs_t{};
    }

    template<std::execution::receiver R>
    auto connect(R rcvr) && {
        return then_op<S, Fn, R>(std::move(sndr_), std::move(fn_), std::move(rcvr));
    }

    template<std::execution::receiver R>
        requires std::copy_constructible<S> && std::copy_constructible<Fn>
    auto connect(R rcvr) const& {
        return then_op<S, Fn, R>(sndr_, fn_, std::move(rcvr));
    }

    auto get_env() const noexcept -> env_of_t<S> {
        return std::execution::get_env(sndr_);
    }
};

// TODO([exec.adapt.obj]): replace per-adaptor friend operator| with
//   sender_adaptor_closure<then_closure<Fn>> CRTP base when adding more adaptors.
template<class Fn>
struct then_closure {
    [[no_unique_address]] Fn fn_;

    template<std::execution::sender S>
        requires std::constructible_from<Fn, Fn&>
    [[nodiscard]] auto operator()(S&& s) & {
        return then_sender<std::decay_t<S>, Fn>{
            __forge_detail::__forward_as_given(std::forward<S>(s)), Fn(fn_)};
    }

    template<std::execution::sender S>
        requires std::constructible_from<Fn, const Fn&>
    [[nodiscard]] auto operator()(S&& s) const & {
        return then_sender<std::decay_t<S>, Fn>{
            __forge_detail::__forward_as_given(std::forward<S>(s)), Fn(fn_)};
    }

    template<std::execution::sender S>
        requires std::constructible_from<Fn, Fn&&>
    [[nodiscard]] auto operator()(S&& s) && {
        return then_sender<std::decay_t<S>, Fn>{
            __forge_detail::__forward_as_given(std::forward<S>(s)), Fn(std::move(fn_))};
    }

    template<std::execution::sender S>
        requires std::constructible_from<Fn, const Fn&&>
    [[nodiscard]] auto operator()(S&& s) const && {
        return then_sender<std::decay_t<S>, Fn>{
            __forge_detail::__forward_as_given(std::forward<S>(s)), Fn(std::move(fn_))};
    }

    // An invalid cvref must not fall back to a different capture category.
    template<class S> void operator()(S&&) & = delete;
    template<class S> void operator()(S&&) const & = delete;
    template<class S> void operator()(S&&) && = delete;
    template<class S> void operator()(S&&) const && = delete;

    template<std::execution::sender S, class Self>
        requires std::same_as<std::remove_cvref_t<Self>, then_closure> &&
                 requires(Self&& self, S&& s) {
                     static_cast<Self&&>(self)(std::forward<S>(s));
                 }
    friend constexpr auto operator|(S&& s, Self&& self) {
        return static_cast<Self&&>(self)(std::forward<S>(s));
    }
};

struct then_t {
    template<std::execution::sender S, class Fn>
    [[nodiscard]] auto operator()(S&& s, Fn&& fn) const {
        return then_sender<std::decay_t<S>, std::decay_t<Fn>>{
            __forge_detail::__forward_as_given(std::forward<S>(s)),
            std::forward<Fn>(fn)};
    }

    template<class Fn>
    [[nodiscard]] auto operator()(Fn&& fn) const {
        return then_closure<std::decay_t<Fn>>{std::forward<Fn>(fn)};
    }
};

} // namespace __forge_then

inline constexpr __forge_then::then_t then{};

} // namespace std::execution
