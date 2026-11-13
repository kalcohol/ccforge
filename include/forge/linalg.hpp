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

template<class Extents, class Layout, class Accessor, class T>
[[nodiscard]] T matrix_frob_norm(
    std::mdspan<typename Accessor::element_type, Extents, Layout, Accessor> x, T init) {
    return std::linalg::__matrix_frob_norm(x, init);
}
template<class Extents, class Layout, class Accessor>
[[nodiscard]] auto matrix_frob_norm(
    std::mdspan<typename Accessor::element_type, Extents, Layout, Accessor> x) {
    return std::linalg::__matrix_frob_norm(x);
}
template<class T>
using sum_of_squares_result = std::linalg::sum_of_squares_result<T>;

template<class Extents, class Layout, class Accessor, class T>
[[nodiscard]] auto vector_sum_of_squares(
    std::mdspan<typename Accessor::element_type, Extents, Layout, Accessor> x,
    sum_of_squares_result<T> init) {
    return std::linalg::__vector_sum_of_squares(x, init);
}
template<class T>
[[nodiscard]] auto setup_givens_rotation(T a, T b) noexcept {
    if constexpr (std::linalg::__detail::__is_complex_v<T>)
        return std::linalg::setup_givens_rotation(a, b);
    else
        return std::linalg::__setup_givens_rotation(a, b);
}
template<class Extents1, class Layout1, class Accessor1,
         class Extents2, class Layout2, class Accessor2, class T>
    requires (std::linalg::__detail::__always_unique_mapping_v<Extents1, Layout1> &&
              std::linalg::__detail::__always_unique_mapping_v<Extents2, Layout2> &&
              std::linalg::__detail::__compatible_static_extents_v<Extents1, Extents2>)
void apply_givens_rotation(
    std::mdspan<typename Accessor1::element_type, Extents1, Layout1, Accessor1> x,
    std::mdspan<typename Accessor2::element_type, Extents2, Layout2, Accessor2> y,
    T c, T s) {
    std::linalg::__apply_givens_rotation(x, y, c, s);
}
template<class Extents1, class Layout1, class Accessor1,
         class Extents2, class Layout2, class Accessor2, class Real>
    requires (std::linalg::__detail::__always_unique_mapping_v<Extents1, Layout1> &&
              std::linalg::__detail::__always_unique_mapping_v<Extents2, Layout2> &&
              std::linalg::__detail::__compatible_static_extents_v<Extents1, Extents2>)
void apply_givens_rotation(
    std::mdspan<std::complex<Real>, Extents1, Layout1, Accessor1> x,
    std::mdspan<std::complex<Real>, Extents2, Layout2, Accessor2> y,
    Real c, std::complex<Real> s) {
    std::linalg::apply_givens_rotation(x, y, c, s);
}

} // namespace forge
#elif defined(__cpp_lib_linalg)
namespace forge {

// Integer semantics are available only with the Forge linalg backport.
template<class... Args>
[[nodiscard]] auto vector_two_norm(Args&&... args) {
    return std::linalg::vector_two_norm(std::forward<Args>(args)...);
}

template<class... Args>
decltype(auto) matrix_frob_norm(Args&&... args) noexcept(noexcept(
    std::linalg::matrix_frob_norm(std::forward<Args>(args)...))) {
    return std::linalg::matrix_frob_norm(std::forward<Args>(args)...);
}

template<class... Args>
decltype(auto) setup_givens_rotation(Args&&... args) noexcept(noexcept(
    std::linalg::setup_givens_rotation(std::forward<Args>(args)...))) {
    return std::linalg::setup_givens_rotation(std::forward<Args>(args)...);
}

template<class... Args>
decltype(auto) apply_givens_rotation(Args&&... args) noexcept(noexcept(
    std::linalg::apply_givens_rotation(std::forward<Args>(args)...))) {
    return std::linalg::apply_givens_rotation(std::forward<Args>(args)...);
}

} // namespace forge
#endif
