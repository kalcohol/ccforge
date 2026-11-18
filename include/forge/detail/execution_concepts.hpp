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

#include <concepts>
#include <execution>
#include <type_traits>
#include <utility>

namespace forge::__detail {

// The draft checks callability here. MANDATE-NOTHROW applies when the completion is invoked.
template<class Signature, class R>
concept __accepts_completion = requires(Signature* __sig) {
    []<class Tag, class... Args>(Tag (*)(Args...))
        requires requires {
            std::declval<Tag>()(std::declval<std::remove_cvref_t<R>>(), std::declval<Args>()...);
        }
    {}(__sig);
};

// Pointer deduction also accepts an accessible, unambiguous derived bundle.
template<class R, class Completions>
concept __accepts_completions = requires(Completions* __completions) {
    []<__accepts_completion<R>... Sigs>(std::execution::completion_signatures<Sigs...>*)
    {}(__completions);
};

template<class R, class Completions>
concept __receiver_of = std::execution::receiver<R> && __accepts_completions<R, Completions>;

template<class S, class R>
concept __sender_to = std::execution::receiver<R> &&
    std::execution::sender_in<S, std::execution::env_of_t<R>> &&
    __receiver_of<R, std::execution::completion_signatures_of_t<S, std::execution::env_of_t<R>>> &&
    requires(S&& sndr, R&& rcvr) {
        std::execution::connect(std::forward<S>(sndr), std::forward<R>(rcvr));
    };

} // namespace forge::__detail
