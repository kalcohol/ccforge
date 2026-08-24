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
#include <concepts>
#include <memory>
#include <type_traits>
#include <utility>

namespace forge {

// Copyable, allocator-neutral stop-token erasure for Forge extension APIs.
class any_stop_token {
public:
    template<class Callback>
    class callback_type;

    template<class Token>
        requires (!std::is_same_v<std::remove_cvref_t<Token>, any_stop_token> &&
                  std::stoppable_token<std::remove_cvref_t<Token>>)
    explicit any_stop_token(Token&& token)
        : impl_(std::make_shared<__impl_t<std::remove_cvref_t<Token>>>(
              std::forward<Token>(token)))
    {}

    any_stop_token() = default;

    any_stop_token(const any_stop_token&) noexcept = default;
    any_stop_token(any_stop_token&&) noexcept = default;

    any_stop_token& operator=(const any_stop_token&) noexcept = default;
    any_stop_token& operator=(any_stop_token&&) noexcept = default;

    friend bool operator==(
        const any_stop_token& left,
        const any_stop_token& right) {
        if (left.impl_ == right.impl_) {
            return true;
        }
        return left.impl_ && right.impl_ &&
            left.impl_->equals(*right.impl_);
    }

    [[nodiscard]] bool stop_requested() const noexcept {
        return impl_ && impl_->stop_requested();
    }

    [[nodiscard]] bool stop_possible() const noexcept {
        return impl_ && impl_->stop_possible();
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
        Callback stored_fn;

        template<class Cb>
        explicit __callback_state(Cb&& callback)
            : stored_fn(std::forward<Cb>(callback))
        {}

        void invoke() noexcept override {
            stored_fn();
        }
    };

    struct __callback_invoker {
        std::shared_ptr<__callback_state_base> state;

        void operator()() noexcept {
            state->invoke();
        }
    };

    struct __noop_callback : __callback_base {};

    struct __base {
        virtual ~__base() = default;
        virtual bool stop_requested() const noexcept = 0;
        virtual bool stop_possible() const noexcept = 0;
        virtual const void* type_key() const noexcept = 0;
        virtual bool equals(const __base& other) const = 0;
        virtual std::unique_ptr<__callback_base> make_callback(
            std::shared_ptr<__callback_state_base> state) const = 0;
    };

    template<class Token>
    inline static unsigned char __type_key = 0;

    template<class Token>
    struct __callback_impl : __callback_base {
        using callback_t =
            std::stop_callback_for_t<Token, __callback_invoker>;

        callback_t registration;

        __callback_impl(
            Token token,
            std::shared_ptr<__callback_state_base> state)
            : registration(
                  std::move(token),
                  __callback_invoker{std::move(state)})
        {}
    };

    template<class Token>
    struct __impl_t : __base {
        Token token;

        explicit __impl_t(Token value) : token(std::move(value)) {}

        bool stop_requested() const noexcept override {
            return token.stop_requested();
        }

        bool stop_possible() const noexcept override {
            return token.stop_possible();
        }

        const void* type_key() const noexcept override {
            return &__type_key<Token>;
        }

        bool equals(const __base& other) const override {
            return type_key() == other.type_key() &&
                token == static_cast<const __impl_t&>(other).token;
        }

        std::unique_ptr<__callback_base> make_callback(
            std::shared_ptr<__callback_state_base> state) const override {
            if constexpr (requires {
                              typename std::stop_callback_for_t<
                                  Token,
                                  __callback_invoker>;
                          }) {
                return std::make_unique<__callback_impl<Token>>(
                    token,
                    std::move(state));
            } else {
                return std::make_unique<__noop_callback>();
            }
        }
    };

    std::shared_ptr<const __base> impl_;
};

template<class Callback>
class any_stop_token::callback_type {
public:
    template<class Cb>
        requires std::constructible_from<Callback, Cb>
    callback_type(any_stop_token token, Cb&& callback)
        : state_(std::make_shared<__callback_state<Callback>>(
              std::forward<Cb>(callback)))
        , handle_(token.impl_
              ? token.impl_->make_callback(state_)
              : std::make_unique<__noop_callback>())
    {}

    callback_type(const callback_type&) = delete;
    callback_type& operator=(const callback_type&) = delete;

private:
    std::shared_ptr<__callback_state_base> state_;
    std::unique_ptr<__callback_base> handle_;
};

} // namespace forge
