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

#include <linalg>
#include <utility>

// The wrapper header can exist even when base mdspan is unavailable.
#if defined(FORGE_LINALG_BACKPORT_ACTIVE)
namespace forge {

// Match the original public parameter forms; helpers add no argument copies.
template<class Extents, class Layout, class Accessor, class T>
[[nodiscard]] T vector_two_norm(
    std::mdspan<typename Accessor::element_type, Extents, Layout, Accessor> x,
    T init) {
    return std::linalg::__vector_two_norm(x, init);
}

template<class Extents, class Layout, class Accessor>
[[nodiscard]] auto vector_two_norm(
    std::mdspan<typename Accessor::element_type, Extents, Layout, Accessor> x) {
    return std::linalg::__vector_two_norm(x);
}

} // namespace forge
#elif defined(__cpp_lib_linalg)
namespace forge {

// Integer semantics are available only with the Forge linalg backport.
template<class... Args>
[[nodiscard]] auto vector_two_norm(Args&&... args) {
    return std::linalg::vector_two_norm(std::forward<Args>(args)...);
}

} // namespace forge
#endif
