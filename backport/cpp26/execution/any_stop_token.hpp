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

#include "stop_token.hpp"

#include <memory>
#include <type_traits>
#include <utility>

namespace std {

class any_stop_token {
public:
    template<class Callback>
    class callback_type;

    template<class Token>
        requires (!std::is_same_v<std::remove_cvref_t<Token>, any_stop_token> &&
                  stoppable_token<std::remove_cvref_t<Token>>)
    explicit any_stop_token(Token&& tok)
        : __impl(std::make_shared<__impl_t<std::remove_cvref_t<Token>>>(
            std::forward<Token>(tok)))
    {}

    any_stop_token() = default;

    any_stop_token(const any_stop_token&) noexcept = default;
    any_stop_token(any_stop_token&&) noexcept = default;

    any_stop_token& operator=(const any_stop_token&) noexcept = default;
    any_stop_token& operator=(any_stop_token&&) noexcept = default;

    friend bool operator==(const any_stop_token&, const any_stop_token&) noexcept = default;

    [[nodiscard]] bool stop_requested() const noexcept {
        return __impl && __impl->stop_requested();
    }

    [[nodiscard]] bool stop_possible() const noexcept {
        return __impl && __impl->stop_possible();
    }

private:
    struct __callback_base {
        virtual ~__callback_base() = default;
    };

    struct __callback_state_base {
        virtual ~__callback_state_base() = default;
        virtual void invoke() noexcept = 0;
    };

    template<class Callback>
    struct __callback_state : __callback_state_base {
        Callback __callback;

        template<class Cb>
        explicit __callback_state(Cb&& cb)
            : __callback(std::forward<Cb>(cb))
        {}

        void invoke() noexcept override {
            __callback();
        }
    };

    struct __callback_invoker {
        std::shared_ptr<__callback_state_base> __state;

        void operator()() noexcept {
            __state->invoke();
        }
    };

    struct __noop_callback : __callback_base {};

    struct __base {
        virtual ~__base() = default;
        virtual bool stop_requested() const noexcept = 0;
        virtual bool stop_possible() const noexcept = 0;
        virtual std::unique_ptr<__callback_base>
            make_callback(std::shared_ptr<__callback_state_base> state) const = 0;
    };

    template<class Token>
    struct __callback_impl : __callback_base {
        using callback_t = stop_callback_for_t<Token, __callback_invoker>;

        callback_t __callback;

        __callback_impl(Token tok, std::shared_ptr<__callback_state_base> state)
            : __callback(std::move(tok), __callback_invoker{std::move(state)})
        {}
    };

    template<class Token>
    struct __impl_t : __base {
        Token __tok;
        explicit __impl_t(Token t) : __tok(std::move(t)) {}
        bool stop_requested() const noexcept override {
            return __tok.stop_requested();
        }
        bool stop_possible() const noexcept override {
            return __tok.stop_possible();
        }
        std::unique_ptr<__callback_base>
        make_callback(std::shared_ptr<__callback_state_base> state) const override {
            if constexpr (requires { typename stop_callback_for_t<Token, __callback_invoker>; }) {
                return std::make_unique<__callback_impl<Token>>(__tok, std::move(state));
            } else {
                return std::make_unique<__noop_callback>();
            }
        }
    };

    std::shared_ptr<const __base> __impl;
};

template<class Callback>
class any_stop_token::callback_type {
public:
    template<class Cb>
        requires std::constructible_from<Callback, Cb>
    callback_type(any_stop_token token, Cb&& cb)
        : __state(std::make_shared<__callback_state<Callback>>(std::forward<Cb>(cb)))
        , __callback(token.__impl
            ? token.__impl->make_callback(__state)
            : std::make_unique<__noop_callback>())
    {}

    callback_type(const callback_type&) = delete;
    callback_type& operator=(const callback_type&) = delete;

private:
    std::shared_ptr<__callback_state_base> __state;
    std::unique_ptr<__callback_base> __callback;
};

} // namespace std
