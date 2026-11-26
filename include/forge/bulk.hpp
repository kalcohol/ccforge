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
#include <utility>

namespace forge {

#if (defined(FORGE_EXECUTION_BACKPORT_ACTIVE) || \
     (defined(FORGE_NATIVE_SENDERS_COMPLETE) && FORGE_NATIVE_SENDERS_COMPLETE) || \
     (!defined(FORGE_NATIVE_SENDERS_COMPLETE) && defined(__cpp_lib_senders) && __cpp_lib_senders >= 202506L)) && \
    ((defined(FORGE_NATIVE_EXECUTION_POLICY_OBJECTS) && FORGE_NATIVE_EXECUTION_POLICY_OBJECTS) || \
     (!defined(FORGE_NATIVE_EXECUTION_POLICY_OBJECTS) && \
      defined(__cpp_lib_execution) && __cpp_lib_execution >= 201603L))
namespace __detail {

template<auto& Adaptor>
struct __sequenced_bulk {
    template<std::execution::sender S, class Shape, class F>
    [[nodiscard]] constexpr auto operator()(S&& sndr, Shape shape, F&& f) const
        noexcept(noexcept(Adaptor(std::forward<S>(sndr), std::execution::seq, shape,
                                  std::forward<F>(f))))
        -> decltype(Adaptor(std::forward<S>(sndr), std::execution::seq, shape, std::forward<F>(f))) {
        return Adaptor(std::forward<S>(sndr), std::execution::seq, shape, std::forward<F>(f));
    }

    template<class Shape, class F>
    [[nodiscard]] constexpr auto operator()(Shape shape, F&& f) const
        noexcept(noexcept(Adaptor(std::execution::seq, shape, std::forward<F>(f))))
        -> decltype(Adaptor(std::execution::seq, shape, std::forward<F>(f))) {
        return Adaptor(std::execution::seq, shape, std::forward<F>(f));
    }
};

} // namespace __detail

inline constexpr __detail::__sequenced_bulk<std::execution::bulk> bulk{};
inline constexpr __detail::__sequenced_bulk<std::execution::bulk_chunked> bulk_chunked{};
inline constexpr __detail::__sequenced_bulk<std::execution::bulk_unchunked> bulk_unchunked{};
#endif

} // namespace forge
