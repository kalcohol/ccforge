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
#include "counting_scope.hpp"
#include "detail/op_storage.hpp"
#include "env.hpp"

#include <exception>
#include <type_traits>
#include <utility>

namespace std::execution {

namespace __forge_associate {

template<class Token, class S>
using __wrapped_sender_t = std::remove_cvref_t<decltype(
    std::declval<Token&>().wrap(std::declval<S>()))>;

template<class S, class R, class Association>
struct __op : __forge_detail::__immovable {
    using operation_state_concept = operation_state_t;

    struct __recv {
        using receiver_concept = receiver_t;

        R* __rcvr;

        template<class... Vs>
        void set_value(Vs&&... vs) && noexcept {
            std::execution::set_value(std::move(*__rcvr), static_cast<Vs&&>(vs)...);
        }

        template<class E>
        void set_error(E&& e) && noexcept {
            std::execution::set_error(std::move(*__rcvr), static_cast<E&&>(e));
        }

        void set_stopped() && noexcept {
            std::execution::set_stopped(std::move(*__rcvr));
        }

        auto get_env() const noexcept -> env_of_t<R> {
            return std::execution::get_env(*__rcvr);
        }
    };

    using inner_op_t = connect_result_t<S, __recv>;

    Association __association;
    S __sndr;
    R __rcvr;
    __forge_detail::__op_storage<1024> __inner_storage;

    __op(S sndr, Association association, R rcvr)
        : __association(std::move(association))
        , __sndr(std::move(sndr))
        , __rcvr(std::move(rcvr))
    {}

    ~__op() noexcept {
        __inner_storage.destroy();
        __association = {};
    }

    void start() & noexcept {
        if (!__association) {
            std::execution::set_stopped(std::move(__rcvr));
            return;
        }

        try {
            auto* op = __inner_storage.template emplace_from<inner_op_t>([&]() -> inner_op_t {
                return std::execution::connect(std::move(__sndr), __recv{&__rcvr});
            });
            std::execution::start(*op);
        } catch (...) {
            std::execution::set_error(std::move(__rcvr), std::current_exception());
        }
    }
};

template<class S, class Token>
struct __sender {
    using sender_concept = sender_t;
    using source_t = S;
    using token_t = Token;
    using wrapped_sender_t = __wrapped_sender_t<Token, S>;
    using association_t = decltype(std::declval<Token&>().try_associate());

    Token __token;
    association_t __association;
    wrapped_sender_t __sndr;

    template<class Source>
    __sender(Source&& sndr, Token token)
        : __token(std::move(token))
        , __association()
        , __sndr(__token.wrap(static_cast<Source&&>(sndr)))
    {
        __association = __token.try_associate();
    }

    __sender(__sender&& other) noexcept(
        std::is_nothrow_move_constructible_v<Token> &&
        std::is_nothrow_move_constructible_v<association_t> &&
        std::is_nothrow_move_constructible_v<wrapped_sender_t>)
        : __token(std::move(other.__token))
        , __association(std::move(other.__association))
        , __sndr(std::move(other.__sndr))
    {}

    __sender(const __sender& other)
        requires std::copy_constructible<wrapped_sender_t>
        : __token(other.__token)
        , __association()
        , __sndr(other.__sndr)
    {
        __association = __token.try_associate();
    }

    __sender& operator=(const __sender&) = delete;
    __sender& operator=(__sender&&) = delete;

    ~__sender() noexcept {
        __association = {};
    }

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept {
        using self_t = std::remove_cvref_t<Self>;
        using up_cs_t = decltype(std::execution::get_completion_signatures(
            std::declval<typename self_t::wrapped_sender_t>(),
            std::declval<Env>()));
        return __forge_meta::__concat_unique_cs_t<
            up_cs_t,
            completion_signatures<set_error_t(std::exception_ptr), set_stopped_t()>>{};
    }

    template<receiver R>
    auto connect(R rcvr) && -> __op<wrapped_sender_t, R, association_t> {
        return __op<wrapped_sender_t, R, association_t>{
            std::move(__sndr), std::move(__association), std::move(rcvr)};
    }

    template<receiver R>
        requires std::copy_constructible<wrapped_sender_t>
    auto connect(R rcvr) const& -> __op<wrapped_sender_t, R, association_t> {
        return __op<wrapped_sender_t, R, association_t>{
            wrapped_sender_t{__sndr}, __token.try_associate(), std::move(rcvr)};
    }

    auto get_env() const noexcept -> env_of_t<wrapped_sender_t> {
        return std::execution::get_env(__sndr);
    }
};

struct __associate_t {
    template<sender S, scope_token Token>
    [[nodiscard]] auto operator()(S&& sndr, Token token) const
        -> __sender<std::decay_t<S>, std::decay_t<Token>> {
        return __sender<std::decay_t<S>, std::decay_t<Token>>{
            static_cast<S&&>(sndr), std::move(token)};
    }
};

} // namespace __forge_associate

inline constexpr __forge_associate::__associate_t associate{};

} // namespace std::execution
