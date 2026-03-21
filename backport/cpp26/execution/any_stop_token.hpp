// MIT License
//
// Copyright (c) 2026 Forge Project
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

namespace std {

class any_stop_token {
public:
    template<class Token>
        requires (stoppable_token<std::remove_cvref_t<Token>> &&
                 !std::is_same_v<std::remove_cvref_t<Token>, any_stop_token>)
    explicit any_stop_token(Token&& tok)
        : __impl(std::make_unique<__impl_t<std::remove_cvref_t<Token>>>(
            std::forward<Token>(tok)))
    {}

    any_stop_token() = default;

    any_stop_token(const any_stop_token& other)
        : __impl(other.__impl ? other.__impl->clone() : nullptr) {}

    any_stop_token(any_stop_token&&) noexcept = default;

    any_stop_token& operator=(const any_stop_token& other) {
        if (this != &other)
            __impl = other.__impl ? other.__impl->clone() : nullptr;
        return *this;
    }
    any_stop_token& operator=(any_stop_token&&) noexcept = default;

    [[nodiscard]] bool stop_requested() const noexcept {
        return __impl && __impl->stop_requested();
    }

    [[nodiscard]] bool stop_possible() const noexcept {
        return __impl && __impl->stop_possible();
    }

private:
    struct __base {
        virtual ~__base() = default;
        virtual bool stop_requested() const noexcept = 0;
        virtual bool stop_possible() const noexcept = 0;
        virtual std::unique_ptr<__base> clone() const = 0;
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
        std::unique_ptr<__base> clone() const override {
            return std::make_unique<__impl_t>(__tok);
        }
    };

    std::unique_ptr<__base> __impl;
};

} // namespace std
