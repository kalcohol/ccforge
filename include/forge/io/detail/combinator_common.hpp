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

#include <forge/io/coro.hpp>
#include <forge/io/result.hpp>

#include <cstddef>
#include <execution>
#include <memory>
#include <stop_token>
#include <type_traits>
#include <utility>

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L

namespace forge::io::__io_combinator_detail {

template<class T>
struct is_io_result : std::false_type {};

template<class... Ts>
struct is_io_result<io_result<Ts...>> : std::true_type {};

template<class T>
inline constexpr bool is_io_result_v = is_io_result<T>::value;

template<std::size_t I, class State, class Result>
struct child_receiver {
    using receiver_concept = std::execution::receiver_t;

    struct env {
        std::shared_ptr<State> state;

        friend auto tag_invoke(
            std::execution::get_stop_token_t,
            const env& self) noexcept -> std::inplace_stop_token {
            return self.state->stop_token();
        }
    };

    std::shared_ptr<State> state;

    auto set_value(Result&& result) && noexcept -> void {
        auto keepalive = state;
        keepalive->template set_value<I>(std::move(result));
    }

    template<class Error>
    auto set_error(Error&& error) && noexcept -> void {
        auto keepalive = state;
        keepalive->template set_error<I>(static_cast<Error&&>(error));
    }

    auto set_stopped() && noexcept -> void {
        auto keepalive = state;
        keepalive->template set_stopped<I>();
    }

    [[nodiscard]] auto get_env() const noexcept -> env {
        return env{state};
    }
};

} // namespace forge::io::__io_combinator_detail

#endif // __cpp_impl_coroutine
