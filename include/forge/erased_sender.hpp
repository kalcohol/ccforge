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

#include <execution>
#include <cstddef>
#include <exception>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>

namespace forge {

template<class CompletionSignatures>
class erased_sender;

namespace __erased_sender_detail {

namespace meta = std::execution::__forge_meta;

template<class Sig>
struct __valid_signature : std::false_type {};

template<class... Vs>
struct __valid_signature<std::execution::set_value_t(Vs...)> : std::true_type {};

template<class E>
struct __valid_signature<std::execution::set_error_t(E)> : std::true_type {};

template<>
struct __valid_signature<std::execution::set_stopped_t()> : std::true_type {};

template<class CS>
struct __valid_completion_signatures : std::false_type {};

template<class... Sigs>
struct __valid_completion_signatures<std::execution::completion_signatures<Sigs...>>
    : std::bool_constant<(__valid_signature<Sigs>::value && ...)> {};

template<class Sig, class CS>
struct __contains_signature;

template<class Sig, class... Sigs>
struct __contains_signature<Sig, std::execution::completion_signatures<Sigs...>>
    : std::bool_constant<(std::is_same_v<Sig, Sigs> || ...)> {};

template<class SourceCS, class TargetCS>
struct __source_is_subset : std::false_type {};

template<class... Sigs, class TargetCS>
struct __source_is_subset<
    std::execution::completion_signatures<Sigs...>,
    TargetCS>
    : std::bool_constant<(__contains_signature<Sigs, TargetCS>::value && ...)> {};

template<class Tuple, class List>
struct __tuple_in_list;

template<class Tuple, class... Tuples>
struct __tuple_in_list<Tuple, meta::type_list<Tuples...>>
    : std::bool_constant<(std::is_same_v<Tuple, Tuples> || ...)> {};

template<class Tuple, class List>
inline constexpr bool __tuple_in_list_v = __tuple_in_list<Tuple, List>::value;

template<class Tuple, class List>
struct __tuple_index;

template<class Tuple, class... Rest>
struct __tuple_index<Tuple, meta::type_list<Tuple, Rest...>>
    : std::integral_constant<std::size_t, 0> {};

template<class Tuple, class First, class... Rest>
struct __tuple_index<Tuple, meta::type_list<First, Rest...>>
    : std::integral_constant<std::size_t,
          1 + __tuple_index<Tuple, meta::type_list<Rest...>>::value> {};

template<class Tuple>
struct __tuple_index<Tuple, meta::type_list<>> {
    static_assert(!std::is_same_v<Tuple, Tuple>,
                  "value tuple is not present in erased_sender completion signatures");
};

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
struct __error_list;

template<class... Sigs>
struct __error_list<std::execution::completion_signatures<Sigs...>> {
    using type = typename __collect_errors<meta::type_list<>, Sigs...>::type;
};

template<class CS>
using __error_list_t = typename __error_list<CS>::type;

template<class Error, class List>
struct __error_in_list;

template<class Error, class... Errors>
struct __error_in_list<Error, meta::type_list<Errors...>>
    : std::bool_constant<(std::is_same_v<Error, Errors> || ...)> {};

template<class Error, class List>
inline constexpr bool __error_in_list_v = __error_in_list<Error, List>::value;

template<class Error, class List>
struct __error_index;

template<class Error, class... Rest>
struct __error_index<Error, meta::type_list<Error, Rest...>>
    : std::integral_constant<std::size_t, 0> {};

template<class Error, class First, class... Rest>
struct __error_index<Error, meta::type_list<First, Rest...>>
    : std::integral_constant<std::size_t,
          1 + __error_index<Error, meta::type_list<Rest...>>::value> {};

template<class Error>
struct __error_index<Error, meta::type_list<>> {
    static_assert(!std::is_same_v<Error, Error>,
                  "error type is not present in erased_sender completion signatures");
};

template<class R>
auto __make_stop_token(const R& rcvr) {
    auto env = std::execution::get_env(rcvr);
    if constexpr (requires { std::execution::get_stop_token(env); }) {
        auto token = std::execution::get_stop_token(env);
        if (token.stop_possible()) {
            return std::any_stop_token{std::move(token)};
        }
    }
    return std::any_stop_token{};
}

template<class CS>
struct __receiver_state_base {
    virtual ~__receiver_state_base() = default;
    virtual void complete_value(std::size_t index, void* tuple) noexcept = 0;
    virtual void complete_error(std::size_t index, void* error) noexcept = 0;
    virtual void complete_stopped() noexcept = 0;
    virtual auto stop_token() const noexcept -> std::any_stop_token = 0;
};

template<class CS>
struct __receiver {
    using receiver_concept = std::execution::receiver_t;
    using value_list = meta::value_tuple_list_t<CS>;
    using error_list = __error_list_t<CS>;

    struct __env {
        std::shared_ptr<__receiver_state_base<CS>> state;

        friend auto tag_invoke(
            std::execution::get_stop_token_t,
            const __env& self) noexcept -> std::any_stop_token {
            if (!self.state) {
                return {};
            }
            return self.state->stop_token();
        }
    };

    std::shared_ptr<__receiver_state_base<CS>> state;

    template<class... Vs>
        requires __tuple_in_list_v<std::tuple<std::decay_t<Vs>...>, value_list>
    void set_value(Vs&&... vs) && noexcept {
        using tuple_t = std::tuple<std::decay_t<Vs>...>;
        auto values = tuple_t{static_cast<Vs&&>(vs)...};
        constexpr auto index = __tuple_index<tuple_t, value_list>::value;
        state->complete_value(index, &values);
    }

    template<class E>
        requires __error_in_list_v<std::decay_t<E>, error_list>
    void set_error(E&& e) && noexcept {
        using error_t = std::decay_t<E>;
        auto error = error_t{static_cast<E&&>(e)};
        constexpr auto index = __error_index<error_t, error_list>::value;
        state->complete_error(index, &error);
    }

    void set_stopped() && noexcept {
        state->complete_stopped();
    }

    auto get_env() const noexcept -> __env {
        return __env{state};
    }
};

template<class CS, class R>
struct __receiver_state_model final : __receiver_state_base<CS> {
    using value_list = meta::value_tuple_list_t<CS>;
    using error_list = __error_list_t<CS>;

    explicit __receiver_state_model(R rcvr)
        : stop_token_(__make_stop_token(rcvr))
        , rcvr_(std::move(rcvr)) {}

    void complete_value(std::size_t index, void* tuple) noexcept override {
        complete_value_impl(index, tuple, value_list{});
    }

    void complete_error(std::size_t index, void* error) noexcept override {
        complete_error_impl(index, error, error_list{});
    }

    void complete_stopped() noexcept override {
        std::execution::set_stopped(std::move(rcvr_));
    }

    auto stop_token() const noexcept -> std::any_stop_token override {
        return stop_token_;
    }

    template<class Tuple, class... Rest>
    void complete_value_impl(std::size_t index, void* tuple, meta::type_list<Tuple, Rest...>) noexcept {
        if (index == 0) {
            auto& values = *static_cast<Tuple*>(tuple);
            std::apply([this](auto&&... vs) noexcept {
                std::execution::set_value(std::move(rcvr_), std::move(vs)...);
            }, std::move(values));
            return;
        }
        complete_value_impl(index - 1, tuple, meta::type_list<Rest...>{});
    }

    void complete_value_impl(std::size_t, void*, meta::type_list<>) noexcept {
        std::terminate();
    }

    template<class Error, class... Rest>
    void complete_error_impl(std::size_t index, void* error, meta::type_list<Error, Rest...>) noexcept {
        if (index == 0) {
            auto& value = *static_cast<Error*>(error);
            std::execution::set_error(std::move(rcvr_), std::move(value));
            return;
        }
        complete_error_impl(index - 1, error, meta::type_list<Rest...>{});
    }

    void complete_error_impl(std::size_t, void*, meta::type_list<>) noexcept {
        std::terminate();
    }

    std::any_stop_token stop_token_;
    R rcvr_;
};

struct __operation_base {
    virtual ~__operation_base() = default;
    virtual void start() noexcept = 0;
};

template<class Op>
struct __operation_model final : __operation_base {
    struct __factory_tag {};

    template<class Factory>
    __operation_model(__factory_tag, Factory&& factory)
        : op_(static_cast<Factory&&>(factory)()) {}

    void start() noexcept override {
        std::execution::start(op_);
    }

    Op op_;
};

template<class CS>
struct __sender_state_base {
    virtual ~__sender_state_base() = default;
    virtual auto connect(__receiver<CS> rcvr) -> std::unique_ptr<__operation_base> = 0;
};

template<class CS, class S>
struct __sender_state_model final : __sender_state_base<CS> {
    template<class Sender>
    explicit __sender_state_model(Sender&& sender)
        : sender_(static_cast<Sender&&>(sender)) {}

    auto connect(__receiver<CS> rcvr) -> std::unique_ptr<__operation_base> override {
        std::lock_guard lk{mtx_};
        using op_t = decltype(std::execution::connect(
            std::declval<S&>(),
            std::declval<__receiver<CS>>()));
        return std::make_unique<__operation_model<op_t>>(
            typename __operation_model<op_t>::__factory_tag{},
            [this, rcvr = std::move(rcvr)]() mutable -> op_t {
                return std::execution::connect(sender_, std::move(rcvr));
            });
    }

    std::mutex mtx_;
    S sender_;
};

template<class S, class CS>
concept __connectable_to_erased_receiver =
    requires(S& sender, __receiver<CS> rcvr) {
        std::execution::connect(sender, std::move(rcvr));
    };

template<class S, class CS>
concept __acceptable_source_sender =
    std::execution::sender_in<S> &&
    __valid_completion_signatures<CS>::value &&
    __valid_completion_signatures<std::execution::completion_signatures_of_t<S>>::value &&
    __source_is_subset<std::execution::completion_signatures_of_t<S>, CS>::value &&
    __connectable_to_erased_receiver<std::remove_cvref_t<S>, CS>;

template<class CS, class R>
auto make_receiver_state(R&& rcvr)
    -> std::shared_ptr<__receiver_state_base<CS>> {
    using receiver_t = std::remove_cvref_t<R>;
    return std::make_shared<__receiver_state_model<CS, receiver_t>>(
        static_cast<R&&>(rcvr));
}

} // namespace __erased_sender_detail

template<class CompletionSignatures>
class erased_sender {
    static_assert(
        __erased_sender_detail::__valid_completion_signatures<CompletionSignatures>::value,
        "forge::erased_sender supports set_value, closed-set set_error(E), and set_stopped signatures");

public:
    using sender_concept = std::execution::sender_t;

    erased_sender() = default;

    template<class S>
        requires (!std::is_same_v<std::remove_cvref_t<S>, erased_sender>)
              && __erased_sender_detail::__acceptable_source_sender<
                     std::remove_cvref_t<S>,
                     CompletionSignatures>
    erased_sender(S&& sender)
        : state_(std::make_shared<
              __erased_sender_detail::__sender_state_model<
                  CompletionSignatures,
                  std::remove_cvref_t<S>>>(
              static_cast<S&&>(sender))) {}

    erased_sender(erased_sender&&) noexcept = default;
    erased_sender& operator=(erased_sender&&) noexcept = default;
    erased_sender(const erased_sender&) = delete;
    erased_sender& operator=(const erased_sender&) = delete;
    ~erased_sender() = default;

    explicit operator bool() const noexcept {
        return static_cast<bool>(state_);
    }

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept -> CompletionSignatures {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }

    template<class R>
    struct __op {
        using operation_state_concept = std::execution::operation_state_t;

        __op(std::shared_ptr<
                 __erased_sender_detail::__sender_state_base<CompletionSignatures>> state,
             R rcvr)
            : state_(std::move(state)) {
            if (!state_) {
                throw std::runtime_error("erased_sender: empty");
            }
            auto erased_rcvr =
                __erased_sender_detail::__receiver<CompletionSignatures>{
                    __erased_sender_detail::make_receiver_state<CompletionSignatures>(
                        std::move(rcvr))};
            op_ = state_->connect(std::move(erased_rcvr));
        }

        __op(__op&&) = delete;
        __op& operator=(__op&&) = delete;
        __op(const __op&) = delete;
        __op& operator=(const __op&) = delete;

        void start() & noexcept {
            op_->start();
        }

        std::shared_ptr<
            __erased_sender_detail::__sender_state_base<CompletionSignatures>> state_;
        std::unique_ptr<__erased_sender_detail::__operation_base> op_;
    };

    template<class R>
        requires std::execution::receiver_of<R, CompletionSignatures>
    auto connect(R rcvr) && -> __op<R> {
        return __op<R>{std::move(state_), std::move(rcvr)};
    }

    template<class R>
        requires std::execution::receiver_of<R, CompletionSignatures>
    auto connect(R rcvr) & -> __op<R> {
        return __op<R>{state_, std::move(rcvr)};
    }

private:
    std::shared_ptr<
        __erased_sender_detail::__sender_state_base<CompletionSignatures>> state_;
};

} // namespace forge
