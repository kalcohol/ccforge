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
#include "env.hpp"
#include "write_env.hpp"

#include <utility>

namespace std::execution {

namespace __forge_unstoppable {

struct __closure {
    template<sender S>
    [[nodiscard]] auto operator()(S&& sndr) const {
        auto env = std::execution::make_env(
            std::execution::make_prop(
                std::execution::get_stop_token_t{}, std::never_stop_token{}));
        return std::execution::write_env(
            __forge_detail::__forward_as_given(std::forward<S>(sndr)), env);
    }

    template<sender S>
    friend constexpr auto operator|(S&& sndr, __closure self) {
        return self(std::forward<S>(sndr));
    }
};

struct __unstoppable_t {
    template<sender S>
    [[nodiscard]] auto operator()(S&& sndr) const {
        return __closure{}(std::forward<S>(sndr));
    }

    [[nodiscard]] auto operator()() const noexcept {
        return __closure{};
    }
};

} // namespace __forge_unstoppable

inline constexpr __forge_unstoppable::__unstoppable_t unstoppable{};

} // namespace std::execution
