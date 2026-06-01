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

#include "detail/completion_meta.hpp"

#include <execution>
#include <exception>
#include <stop_token>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace forge {

struct wait_stopped_t {};

namespace __wait_result_detail {

namespace meta = forge::__detail::meta;

template<class List, class Sig>
struct __push_error {
    using type = List;
};

template<class... Errors, class E>
struct __push_error<meta::type_list<Errors...>, std::execution::set_error_t(E)> {
    using type = meta::list_push_unique_t<
        meta::type_list<Errors...>,
        std::decay_t<E>>;
};

template<class List, class... Sigs>
struct __collect_errors;

template<class List>
struct __collect_errors<List> {
    using type = List;
};

template<class List, class Sig, class... Rest>
struct __collect_errors<List, Sig, Rest...> {
    using next = typename __push_error<List, Sig>::type;
    using type = typename __collect_errors<next, Rest...>::type;
};

template<class CS>
struct __declared_error_list;

template<class... Sigs>
struct __declared_error_list<std::execution::completion_signatures<Sigs...>> {
    using type =
        typename __collect_errors<meta::type_list<>, Sigs...>::type;
};

template<class CS>
using __declared_error_list_t = typename __declared_error_list<CS>::type;

template<class CS>
using __error_list_t = meta::list_push_unique_t<
    __declared_error_list_t<CS>,
    std::exception_ptr>;

template<class List>
struct __single_error_or_variant;

template<class Error>
struct __single_error_or_variant<meta::type_list<Error>> {
    using type = Error;
};

template<class... Errors>
struct __single_error_or_variant<meta::type_list<Errors...>> {
    using type = std::variant<Errors...>;
};

template<class List>
using __single_error_or_variant_t =
    typename __single_error_or_variant<List>::type;

template<class T>
struct __is_variant : std::false_type {};

template<class... Ts>
struct __is_variant<std::variant<Ts...>> : std::true_type {};

template<class T>
inline constexpr bool __is_variant_v = __is_variant<T>::value;

template<class Error, class E>
auto __make_error(E&& e) -> Error {
    using error_t = std::decay_t<E>;
    if constexpr (std::is_same_v<Error, error_t>) {
        return static_cast<E&&>(e);
    } else {
        return Error{std::in_place_type<error_t>, static_cast<E&&>(e)};
    }
}

template<class E, class Error>
auto __error_if(Error& error) noexcept -> E* {
    if constexpr (std::is_same_v<std::remove_cv_t<Error>, E>) {
        return &error;
    } else if constexpr (__is_variant_v<std::remove_cv_t<Error>>) {
        return std::get_if<E>(&error);
    } else {
        return nullptr;
    }
}

template<class E, class Error>
auto __error_if(const Error& error) noexcept -> const E* {
    if constexpr (std::is_same_v<std::remove_cv_t<Error>, E>) {
        return &error;
    } else if constexpr (__is_variant_v<std::remove_cv_t<Error>>) {
        return std::get_if<E>(&error);
    } else {
        return nullptr;
    }
}

template<class State>
struct __receiver {
    using receiver_concept = std::execution::receiver_t;

    State* state;
    std::execution::run_loop* loop;

    template<class... Vs>
    void set_value(Vs&&... vs) && noexcept {
        try {
            using value_t = typename State::value_t;
            using tuple_t = std::tuple<std::decay_t<Vs>...>;
            auto value = meta::value_from_tuple<value_t>(
                tuple_t{static_cast<Vs&&>(vs)...});
            state->set_value(std::move(value));
        } catch (...) {
            state->set_error(std::current_exception());
        }
        loop->finish();
    }

    template<class E>
    void set_error(E&& error) && noexcept {
        state->set_error(static_cast<E&&>(error));
        loop->finish();
    }

    void set_stopped() && noexcept {
        state->set_stopped();
        loop->finish();
    }

    auto get_env() const noexcept {
        return std::execution::make_env(
            std::execution::make_prop(
                std::execution::get_stop_token_t{},
                state->stop_source.get_token()),
            std::execution::make_prop(
                std::execution::get_scheduler_t{},
                loop->get_scheduler()));
    }
};

template<class Value, class Error>
struct __state {
    using value_t = Value;
    using error_t = Error;

    void set_value(value_t value) {
        result.template emplace<1>(std::move(value));
    }

    template<class E>
    void set_error(E&& error) {
        result.template emplace<2>(
            __make_error<error_t>(static_cast<E&&>(error)));
    }

    void set_stopped() {
        result.template emplace<3>();
    }

    std::variant<std::monostate, value_t, error_t, wait_stopped_t> result;
    std::inplace_stop_source stop_source;
};

} // namespace __wait_result_detail

template<class Value, class Error>
class wait_result_value {
public:
    using value_type = Value;
    using error_type = Error;

    wait_result_value() = default;

    [[nodiscard]] bool has_value() const noexcept {
        return result_.index() == 1;
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return has_value();
    }

    [[nodiscard]] bool has_error() const noexcept {
        return result_.index() == 2;
    }

    [[nodiscard]] bool stopped() const noexcept {
        return result_.index() == 3;
    }

    auto value() & -> value_type& {
        return std::get<1>(result_);
    }

    auto value() const& -> const value_type& {
        return std::get<1>(result_);
    }

    auto value() && -> value_type&& {
        return std::move(std::get<1>(result_));
    }

    auto error() & -> error_type& {
        return std::get<2>(result_);
    }

    auto error() const& -> const error_type& {
        return std::get<2>(result_);
    }

    auto error() && -> error_type&& {
        return std::move(std::get<2>(result_));
    }

    template<class E>
    [[nodiscard]] auto error_if() noexcept -> E* {
        if (!has_error()) {
            return nullptr;
        }
        return __wait_result_detail::__error_if<E>(std::get<2>(result_));
    }

    template<class E>
    [[nodiscard]] auto error_if() const noexcept -> const E* {
        if (!has_error()) {
            return nullptr;
        }
        return __wait_result_detail::__error_if<E>(std::get<2>(result_));
    }

private:
    template<std::execution::sender_in S>
    friend auto wait_result_of(S&&);

    explicit wait_result_value(
        std::variant<std::monostate, value_type, error_type, wait_stopped_t>
            result)
        : result_(std::move(result)) {}

    std::variant<std::monostate, value_type, error_type, wait_stopped_t> result_;
};

template<std::execution::sender_in S>
[[nodiscard]] auto wait_result_of(S&& sndr) {
    using cs_t = std::execution::completion_signatures_of_t<S>;
    using value_t = __wait_result_detail::meta::single_value_or_variant_t<cs_t>;
    using error_t = __wait_result_detail::__single_error_or_variant_t<
        __wait_result_detail::__error_list_t<cs_t>>;
    using state_t = __wait_result_detail::__state<value_t, error_t>;
    using receiver_t = __wait_result_detail::__receiver<state_t>;
    using result_t = wait_result_value<value_t, error_t>;

    std::execution::run_loop loop;
    state_t state;
    try {
        auto op = std::execution::connect(
            static_cast<S&&>(sndr),
            receiver_t{&state, &loop});
        std::execution::start(op);
        loop.run();
    } catch (...) {
        state.set_error(std::current_exception());
    }

    return result_t{std::move(state.result)};
}

template<std::execution::sender_in S>
[[nodiscard]] auto wait_result(S&& sndr) {
    return wait_result_of(static_cast<S&&>(sndr));
}

} // namespace forge
