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

#include "concepts.hpp"

namespace std::execution {

template<class Derived>
struct __sender_adaptor_closure {
    template<sender S>
    friend constexpr auto operator|(S&& s, const Derived& self) {
        return self(std::forward<S>(s));
    }
    template<sender S>
    friend constexpr auto operator|(S&& s, Derived&& self) {
        return std::move(self)(std::forward<S>(s));
    }
    template<class D2>
    friend constexpr auto operator|(const Derived& first, const D2& second) {
        return [first, second]<sender S>(S&& s) {
            return second(first(std::forward<S>(s)));
        };
    }
};

} // namespace std::execution
