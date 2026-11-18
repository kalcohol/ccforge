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

#include "level3.hpp"

// Native-only detection happens in detail.hpp before any policy constraints.
#if defined(__cpp_lib_mdspan) && FORGE_NATIVE_EXECUTION_POLICY_OBJECTS
#define FORGE_LINALG_HAS_EXECUTION_POLICY_OVERLOADS 1

namespace std::linalg {

// Every policy uses the sequential twin on the calling thread. The public
// signature can throw; element-access exceptions cross the noexcept lambda
// and terminate. There is no allocation for parallelization.

// copy — policy twin of level1.hpp.
template<class ExecutionPolicy, class InExtents, class InLayout, class InAccessor,
         class OutExtents, class OutLayout, class OutAccessor>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              ((OutExtents::rank() == 1 || OutExtents::rank() == 2) &&
              __detail::__writable_output_v<OutExtents, OutLayout, OutAccessor> &&
              __detail::__compatible_static_extents_v<InExtents, OutExtents>))
void copy(
    ExecutionPolicy&& exec,
    std::mdspan<typename InAccessor::element_type, InExtents, InLayout, InAccessor> from,
    std::mdspan<typename OutAccessor::element_type, OutExtents, OutLayout, OutAccessor> to)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::copy(std::move(from), std::move(to));
    }();
}

// scale — policy twin of level1.hpp.
template<class ExecutionPolicy, class ScalingFactor,
         class Extents, class Layout, class Accessor>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__scalar<ScalingFactor> &&
              (Extents::rank() == 1 || Extents::rank() == 2) &&
              __detail::__writable_output_v<Extents, Layout, Accessor>))
void scale(
    ExecutionPolicy&& exec,
    ScalingFactor alpha,
    std::mdspan<typename Accessor::element_type, Extents, Layout, Accessor> x)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::scale(std::move(alpha), std::move(x));
    }();
}

// swap_elements — policy twin of level1.hpp.
template<class ExecutionPolicy, class Extents1, class Layout1, class Accessor1,
         class Extents2, class Layout2, class Accessor2>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              ((Extents1::rank() == 1 || Extents1::rank() == 2) &&
              __detail::__writable_output_v<Extents1, Layout1, Accessor1> &&
              __detail::__writable_output_v<Extents2, Layout2, Accessor2> &&
              __detail::__compatible_static_extents_v<Extents1, Extents2>))
void swap_elements(
    ExecutionPolicy&& exec,
    std::mdspan<typename Accessor1::element_type, Extents1, Layout1, Accessor1> x,
    std::mdspan<typename Accessor2::element_type, Extents2, Layout2, Accessor2> y)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::swap_elements(std::move(x), std::move(y));
    }();
}

// add — policy twin of level1.hpp.
template<class ExecutionPolicy, class InExtents1, class InLayout1, class InAccessor1,
         class InExtents2, class InLayout2, class InAccessor2,
         class OutExtents, class OutLayout, class OutAccessor>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              ((OutExtents::rank() == 1 || OutExtents::rank() == 2) &&
              __detail::__writable_output_v<OutExtents, OutLayout, OutAccessor> &&
              __detail::__compatible_static_extents_v<InExtents1, InExtents2> &&
             __detail::__compatible_static_extents_v<InExtents1, OutExtents> &&
             __detail::__compatible_static_extents_v<InExtents2, OutExtents>))
void add(
    ExecutionPolicy&& exec,
    std::mdspan<typename InAccessor1::element_type, InExtents1, InLayout1, InAccessor1> x,
    std::mdspan<typename InAccessor2::element_type, InExtents2, InLayout2, InAccessor2> y,
    std::mdspan<typename OutAccessor::element_type, OutExtents, OutLayout, OutAccessor> z)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::add(std::move(x), std::move(y), std::move(z));
    }();
}

// dot — policy twin of level1.hpp.
template<class ExecutionPolicy, class Extents1, class Layout1, class Accessor1,
         class Extents2, class Layout2, class Accessor2,
         class T>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__scalar<T> && Extents1::rank() == 1 && Extents2::rank() == 1 &&
              __detail::__compatible_static_extents_v<Extents1, Extents2>))
T dot(
    ExecutionPolicy&& exec,
    std::mdspan<typename Accessor1::element_type, Extents1, Layout1, Accessor1> x,
    std::mdspan<typename Accessor2::element_type, Extents2, Layout2, Accessor2> y,
    T init)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::dot(std::move(x), std::move(y), std::move(init));
    }();
}

// dot — policy twin of level1.hpp.
template<class ExecutionPolicy, class Extents1, class Layout1, class Accessor1,
         class Extents2, class Layout2, class Accessor2>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (Extents1::rank() == 1 && Extents2::rank() == 1 &&
              __detail::__compatible_static_extents_v<Extents1, Extents2>))
auto dot(
    ExecutionPolicy&& exec,
    std::mdspan<typename Accessor1::element_type, Extents1, Layout1, Accessor1> x,
    std::mdspan<typename Accessor2::element_type, Extents2, Layout2, Accessor2> y)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::dot(std::move(x), std::move(y));
    }();
}

// dotc — policy twin of level1.hpp.
template<class ExecutionPolicy, class Extents1, class Layout1, class Accessor1,
         class Extents2, class Layout2, class Accessor2,
         class T>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__scalar<T> && Extents1::rank() == 1 && Extents2::rank() == 1 &&
              __detail::__compatible_static_extents_v<Extents1, Extents2>))
T dotc(
    ExecutionPolicy&& exec,
    std::mdspan<typename Accessor1::element_type, Extents1, Layout1, Accessor1> x,
    std::mdspan<typename Accessor2::element_type, Extents2, Layout2, Accessor2> y,
    T init)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::dotc(std::move(x), std::move(y), std::move(init));
    }();
}

// dotc — policy twin of level1.hpp.
template<class ExecutionPolicy, class Extents1, class Layout1, class Accessor1,
         class Extents2, class Layout2, class Accessor2>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (Extents1::rank() == 1 && Extents2::rank() == 1 &&
              __detail::__compatible_static_extents_v<Extents1, Extents2>))
auto dotc(
    ExecutionPolicy&& exec,
    std::mdspan<typename Accessor1::element_type, Extents1, Layout1, Accessor1> x,
    std::mdspan<typename Accessor2::element_type, Extents2, Layout2, Accessor2> y)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::dotc(std::move(x), std::move(y));
    }();
}

// vector_two_norm — policy twin of level1.hpp.
template<class ExecutionPolicy, class Extents, class Layout, class Accessor, class T>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              Extents::rank() == 1 &&
              __detail::__norm_arguments<__detail::__accessor_value_t<Accessor>, T>)
T vector_two_norm(
    ExecutionPolicy&& exec,
    std::mdspan<typename Accessor::element_type, Extents, Layout, Accessor> x,
    T init)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::vector_two_norm(std::move(x), std::move(init));
    }();
}

// vector_two_norm — policy twin of level1.hpp.
template<class ExecutionPolicy, class Extents, class Layout, class Accessor>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              Extents::rank() == 1 &&
              __detail::__norm_scalar<__detail::__accessor_value_t<Accessor>>)
auto vector_two_norm(
    ExecutionPolicy&& exec,
    std::mdspan<typename Accessor::element_type, Extents, Layout, Accessor> x)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::vector_two_norm(std::move(x));
    }();
}

// vector_abs_sum — policy twin of level1.hpp.
template<class ExecutionPolicy, class Extents, class Layout, class Accessor, class T>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (Extents::rank() == 1 && __detail::__scalar<T>))
T vector_abs_sum(
    ExecutionPolicy&& exec,
    std::mdspan<typename Accessor::element_type, Extents, Layout, Accessor> x,
    T init)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::vector_abs_sum(std::move(x), std::move(init));
    }();
}

// vector_abs_sum — policy twin of level1.hpp.
template<class ExecutionPolicy, class Extents, class Layout, class Accessor>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (Extents::rank() == 1))
auto vector_abs_sum(
    ExecutionPolicy&& exec,
    std::mdspan<typename Accessor::element_type, Extents, Layout, Accessor> x)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::vector_abs_sum(std::move(x));
    }();
}

// vector_idx_abs_max — policy twin of level1.hpp.
template<class ExecutionPolicy, class Extents, class Layout, class Accessor>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (Extents::rank() == 1))
typename Extents::size_type vector_idx_abs_max(
    ExecutionPolicy&& exec,
    std::mdspan<typename Accessor::element_type, Extents, Layout, Accessor> x)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::vector_idx_abs_max(std::move(x));
    }();
}

// apply_givens_rotation — policy twin of level1.hpp.
template<class ExecutionPolicy, class Extents1, class Layout1, class Accessor1,
         class Extents2, class Layout2, class Accessor2, class T>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__givens_real<T> && Extents1::rank() == 1 &&
              Extents2::rank() == 1 && __detail::__writable_output_v<Extents1, Layout1, Accessor1> &&
              __detail::__writable_output_v<Extents2, Layout2, Accessor2> &&
              __detail::__compatible_static_extents_v<Extents1, Extents2>))
void apply_givens_rotation(
    ExecutionPolicy&& exec,
    std::mdspan<typename Accessor1::element_type, Extents1, Layout1, Accessor1> x,
    std::mdspan<typename Accessor2::element_type, Extents2, Layout2, Accessor2> y,
    T c,
    T s)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::apply_givens_rotation(std::move(x), std::move(y), std::move(c), std::move(s));
    }();
}

// apply_givens_rotation — policy twin of level1.hpp.
template<class ExecutionPolicy, class Extents1, class Layout1, class Accessor1,
         class Extents2, class Layout2, class Accessor2, class Real>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__givens_real<Real> && Extents1::rank() == 1 &&
              Extents2::rank() == 1 && __detail::__writable_output_v<Extents1, Layout1, Accessor1> &&
              __detail::__writable_output_v<Extents2, Layout2, Accessor2> &&
              __detail::__compatible_static_extents_v<Extents1, Extents2>))
void apply_givens_rotation(
    ExecutionPolicy&& exec,
    std::mdspan<typename Accessor1::element_type, Extents1, Layout1, Accessor1> x,
    std::mdspan<typename Accessor2::element_type, Extents2, Layout2, Accessor2> y,
    Real c,
    std::complex<Real> s)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::apply_givens_rotation(std::move(x), std::move(y), std::move(c), std::move(s));
    }();
}

// matrix_frob_norm — policy twin of level1.hpp.
template<class ExecutionPolicy, class Extents, class Layout, class Accessor, class T>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              Extents::rank() == 2 &&
              __detail::__norm_arguments<__detail::__accessor_value_t<Accessor>, T>)
T matrix_frob_norm(
    ExecutionPolicy&& exec,
    std::mdspan<typename Accessor::element_type, Extents, Layout, Accessor> x,
    T init)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::matrix_frob_norm(std::move(x), std::move(init));
    }();
}

// matrix_frob_norm — policy twin of level1.hpp.
template<class ExecutionPolicy, class Extents, class Layout, class Accessor>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              Extents::rank() == 2 &&
              __detail::__norm_scalar<__detail::__accessor_value_t<Accessor>>)
auto matrix_frob_norm(
    ExecutionPolicy&& exec,
    std::mdspan<typename Accessor::element_type, Extents, Layout, Accessor> x)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::matrix_frob_norm(std::move(x));
    }();
}

// matrix_one_norm — policy twin of level1.hpp.
template<class ExecutionPolicy, class Extents, class Layout, class Accessor, class T>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (Extents::rank() == 2 && __detail::__scalar<T>))
T matrix_one_norm(
    ExecutionPolicy&& exec,
    std::mdspan<typename Accessor::element_type, Extents, Layout, Accessor> A,
    T init)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::matrix_one_norm(std::move(A), std::move(init));
    }();
}

// matrix_one_norm — policy twin of level1.hpp.
template<class ExecutionPolicy, class Extents, class Layout, class Accessor>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (Extents::rank() == 2))
auto matrix_one_norm(
    ExecutionPolicy&& exec,
    std::mdspan<typename Accessor::element_type, Extents, Layout, Accessor> A)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::matrix_one_norm(std::move(A));
    }();
}

// matrix_inf_norm — policy twin of level1.hpp.
template<class ExecutionPolicy, class Extents, class Layout, class Accessor, class T>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (Extents::rank() == 2 && __detail::__scalar<T>))
T matrix_inf_norm(
    ExecutionPolicy&& exec,
    std::mdspan<typename Accessor::element_type, Extents, Layout, Accessor> A,
    T init)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::matrix_inf_norm(std::move(A), std::move(init));
    }();
}

// matrix_inf_norm — policy twin of level1.hpp.
template<class ExecutionPolicy, class Extents, class Layout, class Accessor>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (Extents::rank() == 2))
auto matrix_inf_norm(
    ExecutionPolicy&& exec,
    std::mdspan<typename Accessor::element_type, Extents, Layout, Accessor> A)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::matrix_inf_norm(std::move(A));
    }();
}

// matrix_vector_product — policy twin of level2.hpp.
template<class ExecutionPolicy, class AExtents, class ALayout, class AAccessor,
         class XExtents, class XLayout, class XAccessor,
         class YExtents, class YLayout, class YAccessor>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__writable_output_v<YExtents, YLayout, YAccessor> &&
              AExtents::rank() == 2 &&
              XExtents::rank() == 1 &&
              YExtents::rank() == 1 &&
              __detail::__compatible_static_extent_v<AExtents, 1, XExtents, 0> &&
              __detail::__compatible_static_extent_v<AExtents, 0, YExtents, 0>))
void matrix_vector_product(
    ExecutionPolicy&& exec,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x,
    std::mdspan<typename YAccessor::element_type, YExtents, YLayout, YAccessor> y)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::matrix_vector_product(std::move(A), std::move(x), std::move(y));
    }();
}

// matrix_vector_product — policy twin of level2.hpp.
template<class ExecutionPolicy, class AExtents, class ALayout, class AAccessor,
         class XExtents, class XLayout, class XAccessor,
         class YExtents, class YLayout, class YAccessor,
         class ZExtents, class ZLayout, class ZAccessor>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__writable_output_v<ZExtents, ZLayout, ZAccessor> &&
              AExtents::rank() == 2 &&
              XExtents::rank() == 1 &&
              YExtents::rank() == 1 &&
              ZExtents::rank() == 1 &&
              __detail::__compatible_static_extent_v<AExtents, 1, XExtents, 0> &&
              __detail::__compatible_static_extent_v<AExtents, 0, YExtents, 0> &&
              __detail::__compatible_static_extent_v<AExtents, 0, ZExtents, 0> &&
              __detail::__compatible_static_extent_v<YExtents, 0, ZExtents, 0>))
void matrix_vector_product(
    ExecutionPolicy&& exec,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x,
    std::mdspan<typename YAccessor::element_type, YExtents, YLayout, YAccessor> y,
    std::mdspan<typename ZAccessor::element_type, ZExtents, ZLayout, ZAccessor> z)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::matrix_vector_product(std::move(A), std::move(x), std::move(y), std::move(z));
    }();
}

// triangular_matrix_vector_product — policy twin of level2.hpp.
template<class ExecutionPolicy, class Triangle, class DiagonalStorage,
         class AExtents, class ALayout, class AAccessor,
         class XExtents, class XLayout, class XAccessor>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__writable_output_v<XExtents, XLayout, XAccessor> &&
              __detail::__is_diagonal_storage_v<DiagonalStorage> &&
              __detail::__square_static_extents_v<AExtents> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle> &&
              XExtents::rank() == 1 &&
              __detail::__compatible_static_extent_v<AExtents, 0, XExtents, 0> &&
              __detail::__compatible_static_extent_v<AExtents, 1, XExtents, 0>))
void triangular_matrix_vector_product(
    ExecutionPolicy&& exec,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle t,
    DiagonalStorage d,
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::triangular_matrix_vector_product(std::move(A), std::move(t), std::move(d), std::move(x));
    }();
}

// triangular_matrix_vector_product — policy twin of level2.hpp.
template<class ExecutionPolicy, class Triangle, class DiagonalStorage,
         class AExtents, class ALayout, class AAccessor,
         class XExtents, class XLayout, class XAccessor,
         class YExtents, class YLayout, class YAccessor>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__writable_output_v<YExtents, YLayout, YAccessor> &&
              __detail::__is_diagonal_storage_v<DiagonalStorage> &&
              __detail::__square_static_extents_v<AExtents> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle> &&
              XExtents::rank() == 1 &&
              YExtents::rank() == 1 &&
              __detail::__compatible_static_extent_v<AExtents, 0, XExtents, 0> &&
              __detail::__compatible_static_extent_v<AExtents, 1, XExtents, 0> &&
              __detail::__compatible_static_extent_v<AExtents, 0, YExtents, 0> &&
              __detail::__compatible_static_extent_v<AExtents, 1, YExtents, 0> &&
              __detail::__compatible_static_extent_v<XExtents, 0, YExtents, 0>))
void triangular_matrix_vector_product(
    ExecutionPolicy&& exec,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle t,
    DiagonalStorage d,
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x,
    std::mdspan<typename YAccessor::element_type, YExtents, YLayout, YAccessor> y)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::triangular_matrix_vector_product(std::move(A), std::move(t), std::move(d), std::move(x), std::move(y));
    }();
}

// symmetric_matrix_vector_product — policy twin of level2.hpp.
template<class ExecutionPolicy, class Triangle,
         class AExtents, class ALayout, class AAccessor,
         class XExtents, class XLayout, class XAccessor,
         class YExtents, class YLayout, class YAccessor>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__writable_output_v<YExtents, YLayout, YAccessor> &&
              __detail::__square_static_extents_v<AExtents> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle> &&
              XExtents::rank() == 1 &&
              YExtents::rank() == 1 &&
              __detail::__compatible_static_extent_v<AExtents, 0, XExtents, 0> &&
              __detail::__compatible_static_extent_v<AExtents, 1, XExtents, 0> &&
              __detail::__compatible_static_extent_v<AExtents, 0, YExtents, 0> &&
              __detail::__compatible_static_extent_v<AExtents, 1, YExtents, 0> &&
              __detail::__compatible_static_extent_v<XExtents, 0, YExtents, 0>))
void symmetric_matrix_vector_product(
    ExecutionPolicy&& exec,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle t,
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x,
    std::mdspan<typename YAccessor::element_type, YExtents, YLayout, YAccessor> y)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::symmetric_matrix_vector_product(std::move(A), std::move(t), std::move(x), std::move(y));
    }();
}

// symmetric_matrix_vector_product — policy twin of level2.hpp.
template<class ExecutionPolicy, class Triangle,
         class AExtents, class ALayout, class AAccessor,
         class XExtents, class XLayout, class XAccessor,
         class YExtents, class YLayout, class YAccessor,
         class ZExtents, class ZLayout, class ZAccessor>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__writable_output_v<ZExtents, ZLayout, ZAccessor> &&
              __detail::__square_static_extents_v<AExtents> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle> &&
              XExtents::rank() == 1 &&
              YExtents::rank() == 1 &&
              ZExtents::rank() == 1 &&
              __detail::__compatible_static_extent_v<AExtents, 0, XExtents, 0> &&
              __detail::__compatible_static_extent_v<AExtents, 1, XExtents, 0> &&
              __detail::__compatible_static_extent_v<AExtents, 0, YExtents, 0> &&
              __detail::__compatible_static_extent_v<AExtents, 1, YExtents, 0> &&
              __detail::__compatible_static_extent_v<AExtents, 0, ZExtents, 0> &&
              __detail::__compatible_static_extent_v<AExtents, 1, ZExtents, 0> &&
              __detail::__compatible_static_extent_v<XExtents, 0, YExtents, 0> &&
              __detail::__compatible_static_extent_v<XExtents, 0, ZExtents, 0> &&
              __detail::__compatible_static_extent_v<YExtents, 0, ZExtents, 0>))
void symmetric_matrix_vector_product(
    ExecutionPolicy&& exec,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle t,
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x,
    std::mdspan<typename YAccessor::element_type, YExtents, YLayout, YAccessor> y,
    std::mdspan<typename ZAccessor::element_type, ZExtents, ZLayout, ZAccessor> z)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::symmetric_matrix_vector_product(std::move(A), std::move(t), std::move(x), std::move(y), std::move(z));
    }();
}

// hermitian_matrix_vector_product — policy twin of level2.hpp.
template<class ExecutionPolicy, class Triangle,
         class AExtents, class ALayout, class AAccessor,
         class XExtents, class XLayout, class XAccessor,
         class YExtents, class YLayout, class YAccessor>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__writable_output_v<YExtents, YLayout, YAccessor> &&
              __detail::__square_static_extents_v<AExtents> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle> &&
              XExtents::rank() == 1 &&
              YExtents::rank() == 1 &&
              __detail::__compatible_static_extent_v<AExtents, 0, XExtents, 0> &&
              __detail::__compatible_static_extent_v<AExtents, 1, XExtents, 0> &&
              __detail::__compatible_static_extent_v<AExtents, 0, YExtents, 0> &&
              __detail::__compatible_static_extent_v<AExtents, 1, YExtents, 0> &&
              __detail::__compatible_static_extent_v<XExtents, 0, YExtents, 0>))
void hermitian_matrix_vector_product(
    ExecutionPolicy&& exec,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle t,
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x,
    std::mdspan<typename YAccessor::element_type, YExtents, YLayout, YAccessor> y)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::hermitian_matrix_vector_product(std::move(A), std::move(t), std::move(x), std::move(y));
    }();
}

// hermitian_matrix_vector_product — policy twin of level2.hpp.
template<class ExecutionPolicy, class Triangle,
         class AExtents, class ALayout, class AAccessor,
         class XExtents, class XLayout, class XAccessor,
         class YExtents, class YLayout, class YAccessor,
         class ZExtents, class ZLayout, class ZAccessor>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__writable_output_v<ZExtents, ZLayout, ZAccessor> &&
              __detail::__square_static_extents_v<AExtents> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle> &&
              XExtents::rank() == 1 &&
              YExtents::rank() == 1 &&
              ZExtents::rank() == 1 &&
              __detail::__compatible_static_extent_v<AExtents, 0, XExtents, 0> &&
              __detail::__compatible_static_extent_v<AExtents, 1, XExtents, 0> &&
              __detail::__compatible_static_extent_v<AExtents, 0, YExtents, 0> &&
              __detail::__compatible_static_extent_v<AExtents, 1, YExtents, 0> &&
              __detail::__compatible_static_extent_v<AExtents, 0, ZExtents, 0> &&
              __detail::__compatible_static_extent_v<AExtents, 1, ZExtents, 0> &&
              __detail::__compatible_static_extent_v<XExtents, 0, YExtents, 0> &&
              __detail::__compatible_static_extent_v<XExtents, 0, ZExtents, 0> &&
              __detail::__compatible_static_extent_v<YExtents, 0, ZExtents, 0>))
void hermitian_matrix_vector_product(
    ExecutionPolicy&& exec,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle t,
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x,
    std::mdspan<typename YAccessor::element_type, YExtents, YLayout, YAccessor> y,
    std::mdspan<typename ZAccessor::element_type, ZExtents, ZLayout, ZAccessor> z)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::hermitian_matrix_vector_product(std::move(A), std::move(t), std::move(x), std::move(y), std::move(z));
    }();
}

// matrix_rank_1_update — policy twin of level2.hpp.
template<class ExecutionPolicy, class XExtents, class XLayout, class XAccessor,
         class YExtents, class YLayout, class YAccessor,
         class AExtents, class ALayout, class AAccessor>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__writable_output_v<AExtents, ALayout, AAccessor> &&
              XExtents::rank() == 1 &&
              YExtents::rank() == 1 &&
              AExtents::rank() == 2 &&
              __detail::__compatible_static_extent_v<XExtents, 0, AExtents, 0> &&
              __detail::__compatible_static_extent_v<YExtents, 0, AExtents, 1>))
void matrix_rank_1_update(
    ExecutionPolicy&& exec,
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x,
    std::mdspan<typename YAccessor::element_type, YExtents, YLayout, YAccessor> y,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::matrix_rank_1_update(std::move(x), std::move(y), std::move(A));
    }();
}

// matrix_rank_1_update — policy twin of level2.hpp.
template<class ExecutionPolicy, class XExtents, class XLayout, class XAccessor,
         class YExtents, class YLayout, class YAccessor,
         class EExtents, class ELayout, class EAccessor,
         class AExtents, class ALayout, class AAccessor>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__writable_output_v<AExtents, ALayout, AAccessor> &&
              XExtents::rank() == 1 &&
              YExtents::rank() == 1 &&
              EExtents::rank() == 2 &&
              AExtents::rank() == 2 &&
              __detail::__compatible_static_extent_v<XExtents, 0, EExtents, 0> &&
              __detail::__compatible_static_extent_v<XExtents, 0, AExtents, 0> &&
              __detail::__compatible_static_extent_v<YExtents, 0, EExtents, 1> &&
              __detail::__compatible_static_extent_v<YExtents, 0, AExtents, 1> &&
              __detail::__compatible_static_extents_v<EExtents, AExtents>))
void matrix_rank_1_update(
    ExecutionPolicy&& exec,
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x,
    std::mdspan<typename YAccessor::element_type, YExtents, YLayout, YAccessor> y,
    std::mdspan<typename EAccessor::element_type, EExtents, ELayout, EAccessor> E,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::matrix_rank_1_update(std::move(x), std::move(y), std::move(E), std::move(A));
    }();
}

// matrix_rank_1_update_c — policy twin of level2.hpp.
template<class ExecutionPolicy, class XExtents, class XLayout, class XAccessor,
         class YExtents, class YLayout, class YAccessor,
         class AExtents, class ALayout, class AAccessor>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__writable_output_v<AExtents, ALayout, AAccessor> &&
              XExtents::rank() == 1 &&
              YExtents::rank() == 1 &&
              AExtents::rank() == 2 &&
              __detail::__compatible_static_extent_v<XExtents, 0, AExtents, 0> &&
              __detail::__compatible_static_extent_v<YExtents, 0, AExtents, 1>))
void matrix_rank_1_update_c(
    ExecutionPolicy&& exec,
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x,
    std::mdspan<typename YAccessor::element_type, YExtents, YLayout, YAccessor> y,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::matrix_rank_1_update_c(std::move(x), std::move(y), std::move(A));
    }();
}

// matrix_rank_1_update_c — policy twin of level2.hpp.
template<class ExecutionPolicy, class XExtents, class XLayout, class XAccessor,
         class YExtents, class YLayout, class YAccessor,
         class EExtents, class ELayout, class EAccessor,
         class AExtents, class ALayout, class AAccessor>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__writable_output_v<AExtents, ALayout, AAccessor> &&
              XExtents::rank() == 1 &&
              YExtents::rank() == 1 &&
              EExtents::rank() == 2 &&
              AExtents::rank() == 2 &&
              __detail::__compatible_static_extent_v<XExtents, 0, EExtents, 0> &&
              __detail::__compatible_static_extent_v<XExtents, 0, AExtents, 0> &&
              __detail::__compatible_static_extent_v<YExtents, 0, EExtents, 1> &&
              __detail::__compatible_static_extent_v<YExtents, 0, AExtents, 1> &&
              __detail::__compatible_static_extents_v<EExtents, AExtents>))
void matrix_rank_1_update_c(
    ExecutionPolicy&& exec,
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x,
    std::mdspan<typename YAccessor::element_type, YExtents, YLayout, YAccessor> y,
    std::mdspan<typename EAccessor::element_type, EExtents, ELayout, EAccessor> E,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::matrix_rank_1_update_c(std::move(x), std::move(y), std::move(E), std::move(A));
    }();
}

// symmetric_matrix_rank_1_update — policy twin of level2.hpp.
template<class ExecutionPolicy, class ScalingFactor,
         class XExtents, class XLayout, class XAccessor,
         class AExtents, class ALayout, class AAccessor,
         class Triangle>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__scalar<ScalingFactor> &&
              __detail::__possibly_packed_output_v<AExtents, ALayout, AAccessor> &&
              __detail::__square_matrix_vector_static_extents_v<AExtents, XExtents> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle>))
void symmetric_matrix_rank_1_update(
    ExecutionPolicy&& exec,
    ScalingFactor alpha,
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle t)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::symmetric_matrix_rank_1_update(std::move(alpha), std::move(x), std::move(A), std::move(t));
    }();
}

// symmetric_matrix_rank_1_update — policy twin of level2.hpp.
template<class ExecutionPolicy, class ScalingFactor,
         class XExtents, class XLayout, class XAccessor,
         class EExtents, class ELayout, class EAccessor,
         class AExtents, class ALayout, class AAccessor,
         class Triangle>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__scalar<ScalingFactor> &&
              __detail::__possibly_packed_output_v<AExtents, ALayout, AAccessor> &&
              __detail::__square_matrix_vector_static_extents_v<EExtents, XExtents> &&
              __detail::__square_matrix_vector_static_extents_v<AExtents, XExtents> &&
              __detail::__compatible_static_extents_v<EExtents, AExtents> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle>))
void symmetric_matrix_rank_1_update(
    ExecutionPolicy&& exec,
    ScalingFactor alpha,
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x,
    std::mdspan<typename EAccessor::element_type, EExtents, ELayout, EAccessor> E,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle t)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::symmetric_matrix_rank_1_update(std::move(alpha), std::move(x), std::move(E), std::move(A), std::move(t));
    }();
}

// symmetric_matrix_rank_2_update — policy twin of level2.hpp.
template<class ExecutionPolicy, class XExtents, class XLayout, class XAccessor,
         class YExtents, class YLayout, class YAccessor,
         class AExtents, class ALayout, class AAccessor,
         class Triangle>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__possibly_packed_output_v<AExtents, ALayout, AAccessor> &&
              __detail::__square_matrix_vectors_static_extents_v<AExtents, XExtents, YExtents> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle>))
void symmetric_matrix_rank_2_update(
    ExecutionPolicy&& exec,
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x,
    std::mdspan<typename YAccessor::element_type, YExtents, YLayout, YAccessor> y,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle t)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::symmetric_matrix_rank_2_update(std::move(x), std::move(y), std::move(A), std::move(t));
    }();
}

// symmetric_matrix_rank_2_update — policy twin of level2.hpp.
template<class ExecutionPolicy, class XExtents, class XLayout, class XAccessor,
         class YExtents, class YLayout, class YAccessor,
         class EExtents, class ELayout, class EAccessor,
         class AExtents, class ALayout, class AAccessor,
         class Triangle>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__possibly_packed_output_v<AExtents, ALayout, AAccessor> &&
              __detail::__square_matrix_vectors_static_extents_v<EExtents, XExtents, YExtents> &&
              __detail::__square_matrix_vectors_static_extents_v<AExtents, XExtents, YExtents> &&
              __detail::__compatible_static_extents_v<EExtents, AExtents> &&
              __detail::__packed_triangle_matches_v<ELayout, Triangle> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle>))
void symmetric_matrix_rank_2_update(
    ExecutionPolicy&& exec,
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x,
    std::mdspan<typename YAccessor::element_type, YExtents, YLayout, YAccessor> y,
    std::mdspan<typename EAccessor::element_type, EExtents, ELayout, EAccessor> E,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle t)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::symmetric_matrix_rank_2_update(std::move(x), std::move(y), std::move(E), std::move(A), std::move(t));
    }();
}

// hermitian_matrix_rank_1_update — policy twin of level2.hpp.
template<class ExecutionPolicy, class ScalingFactor,
         class XExtents, class XLayout, class XAccessor,
         class AExtents, class ALayout, class AAccessor,
         class Triangle>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__scalar<ScalingFactor> &&
              __detail::__possibly_packed_output_v<AExtents, ALayout, AAccessor> &&
              __detail::__square_matrix_vector_static_extents_v<AExtents, XExtents> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle>))
void hermitian_matrix_rank_1_update(
    ExecutionPolicy&& exec,
    ScalingFactor alpha,
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle t)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::hermitian_matrix_rank_1_update(std::move(alpha), std::move(x), std::move(A), std::move(t));
    }();
}

// hermitian_matrix_rank_1_update — policy twin of level2.hpp.
template<class ExecutionPolicy, class ScalingFactor,
         class XExtents, class XLayout, class XAccessor,
         class EExtents, class ELayout, class EAccessor,
         class AExtents, class ALayout, class AAccessor,
         class Triangle>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__scalar<ScalingFactor> &&
              __detail::__possibly_packed_output_v<AExtents, ALayout, AAccessor> &&
              __detail::__square_matrix_vector_static_extents_v<EExtents, XExtents> &&
              __detail::__square_matrix_vector_static_extents_v<AExtents, XExtents> &&
              __detail::__compatible_static_extents_v<EExtents, AExtents> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle>))
void hermitian_matrix_rank_1_update(
    ExecutionPolicy&& exec,
    ScalingFactor alpha,
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x,
    std::mdspan<typename EAccessor::element_type, EExtents, ELayout, EAccessor> E,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle t)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::hermitian_matrix_rank_1_update(std::move(alpha), std::move(x), std::move(E), std::move(A), std::move(t));
    }();
}

// hermitian_matrix_rank_2_update — policy twin of level2.hpp.
template<class ExecutionPolicy, class XExtents, class XLayout, class XAccessor,
         class YExtents, class YLayout, class YAccessor,
         class AExtents, class ALayout, class AAccessor,
         class Triangle>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__possibly_packed_output_v<AExtents, ALayout, AAccessor> &&
              __detail::__square_matrix_vectors_static_extents_v<AExtents, XExtents, YExtents> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle>))
void hermitian_matrix_rank_2_update(
    ExecutionPolicy&& exec,
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x,
    std::mdspan<typename YAccessor::element_type, YExtents, YLayout, YAccessor> y,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle t)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::hermitian_matrix_rank_2_update(std::move(x), std::move(y), std::move(A), std::move(t));
    }();
}

// hermitian_matrix_rank_2_update — policy twin of level2.hpp.
template<class ExecutionPolicy, class XExtents, class XLayout, class XAccessor,
         class YExtents, class YLayout, class YAccessor,
         class EExtents, class ELayout, class EAccessor,
         class AExtents, class ALayout, class AAccessor,
         class Triangle>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__possibly_packed_output_v<AExtents, ALayout, AAccessor> &&
              __detail::__square_matrix_vectors_static_extents_v<EExtents, XExtents, YExtents> &&
              __detail::__square_matrix_vectors_static_extents_v<AExtents, XExtents, YExtents> &&
              __detail::__compatible_static_extents_v<EExtents, AExtents> &&
              __detail::__packed_triangle_matches_v<ELayout, Triangle> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle>))
void hermitian_matrix_rank_2_update(
    ExecutionPolicy&& exec,
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x,
    std::mdspan<typename YAccessor::element_type, YExtents, YLayout, YAccessor> y,
    std::mdspan<typename EAccessor::element_type, EExtents, ELayout, EAccessor> E,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle t)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::hermitian_matrix_rank_2_update(std::move(x), std::move(y), std::move(E), std::move(A), std::move(t));
    }();
}

// triangular_matrix_vector_solve — policy twin of level2.hpp.
template<class ExecutionPolicy, class Triangle, class DiagonalStorage,
         class AExtents, class ALayout, class AAccessor,
         class BExtents, class BLayout, class BAccessor,
         class XExtents, class XLayout, class XAccessor,
         class BinaryDivideOp>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__writable_output_v<XExtents, XLayout, XAccessor> &&
              __detail::__is_diagonal_storage_v<DiagonalStorage> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle> &&
              __detail::__square_matrix_vectors_static_extents_v<AExtents, BExtents, XExtents>))
void triangular_matrix_vector_solve(
    ExecutionPolicy&& exec,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle t,
    DiagonalStorage d,
    std::mdspan<typename BAccessor::element_type, BExtents, BLayout, BAccessor> b,
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x,
    BinaryDivideOp divide)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::triangular_matrix_vector_solve(std::move(A), std::move(t), std::move(d), std::move(b), std::move(x), std::move(divide));
    }();
}

// triangular_matrix_vector_solve — policy twin of level2.hpp.
template<class ExecutionPolicy, class Triangle, class DiagonalStorage,
         class AExtents, class ALayout, class AAccessor,
         class BExtents, class BLayout, class BAccessor,
         class XExtents, class XLayout, class XAccessor>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__writable_output_v<XExtents, XLayout, XAccessor> &&
              __detail::__is_diagonal_storage_v<DiagonalStorage> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle> &&
              __detail::__square_matrix_vectors_static_extents_v<AExtents, BExtents, XExtents>))
void triangular_matrix_vector_solve(
    ExecutionPolicy&& exec,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle t,
    DiagonalStorage d,
    std::mdspan<typename BAccessor::element_type, BExtents, BLayout, BAccessor> b,
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::triangular_matrix_vector_solve(std::move(A), std::move(t), std::move(d), std::move(b), std::move(x));
    }();
}

// triangular_matrix_vector_solve — policy twin of level2.hpp.
template<class ExecutionPolicy, class Triangle, class DiagonalStorage,
         class AExtents, class ALayout, class AAccessor,
         class BExtents, class BLayout, class BAccessor,
         class BinaryDivideOp>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__writable_output_v<BExtents, BLayout, BAccessor> &&
              __detail::__is_diagonal_storage_v<DiagonalStorage> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle> &&
              __detail::__square_matrix_vector_static_extents_v<AExtents, BExtents>))
void triangular_matrix_vector_solve(
    ExecutionPolicy&& exec,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle t,
    DiagonalStorage d,
    std::mdspan<typename BAccessor::element_type, BExtents, BLayout, BAccessor> b,
    BinaryDivideOp divide)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::triangular_matrix_vector_solve(std::move(A), std::move(t), std::move(d), std::move(b), std::move(divide));
    }();
}

// triangular_matrix_vector_solve — policy twin of level2.hpp.
template<class ExecutionPolicy, class Triangle, class DiagonalStorage,
         class AExtents, class ALayout, class AAccessor,
         class BExtents, class BLayout, class BAccessor>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__writable_output_v<BExtents, BLayout, BAccessor> &&
              __detail::__is_diagonal_storage_v<DiagonalStorage> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle> &&
              __detail::__square_matrix_vector_static_extents_v<AExtents, BExtents>))
void triangular_matrix_vector_solve(
    ExecutionPolicy&& exec,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle t,
    DiagonalStorage d,
    std::mdspan<typename BAccessor::element_type, BExtents, BLayout, BAccessor> b)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::triangular_matrix_vector_solve(std::move(A), std::move(t), std::move(d), std::move(b));
    }();
}

// triangular_matrix_vector_product — policy twin of level2.hpp.
template<class ExecutionPolicy, class Triangle, class DiagonalStorage,
         class AExtents, class ALayout, class AAccessor,
         class XExtents, class XLayout, class XAccessor,
         class YExtents, class YLayout, class YAccessor,
         class ZExtents, class ZLayout, class ZAccessor>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__writable_output_v<ZExtents, ZLayout, ZAccessor> &&
              __detail::__is_diagonal_storage_v<DiagonalStorage> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle> &&
              __detail::__square_matrix_vectors_static_extents_v<AExtents, XExtents, YExtents> &&
              __detail::__square_matrix_vector_static_extents_v<AExtents, ZExtents> &&
              __detail::__compatible_static_extents_v<YExtents, ZExtents>))
void triangular_matrix_vector_product(
    ExecutionPolicy&& exec,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle t,
    DiagonalStorage d,
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x,
    std::mdspan<typename YAccessor::element_type, YExtents, YLayout, YAccessor> y,
    std::mdspan<typename ZAccessor::element_type, ZExtents, ZLayout, ZAccessor> z)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::triangular_matrix_vector_product(std::move(A), std::move(t), std::move(d), std::move(x), std::move(y), std::move(z));
    }();
}

// matrix_product — policy twin of level3.hpp.
template<class ExecutionPolicy, class AExtents, class ALayout, class AAccessor,
         class BExtents, class BLayout, class BAccessor,
         class CExtents, class CLayout, class CAccessor>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__writable_output_v<CExtents, CLayout, CAccessor> &&
              __detail::__matrix_product_static_extents_v<AExtents, BExtents, CExtents>))
void matrix_product(
    ExecutionPolicy&& exec,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    std::mdspan<typename BAccessor::element_type, BExtents, BLayout, BAccessor> B,
    std::mdspan<typename CAccessor::element_type, CExtents, CLayout, CAccessor> C)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::matrix_product(std::move(A), std::move(B), std::move(C));
    }();
}

// matrix_product — policy twin of level3.hpp.
template<class ExecutionPolicy, class AExtents, class ALayout, class AAccessor,
         class BExtents, class BLayout, class BAccessor,
         class EExtents, class ELayout, class EAccessor,
         class CExtents, class CLayout, class CAccessor>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__writable_output_v<CExtents, CLayout, CAccessor> &&
              __detail::__matrix_product_static_extents_v<AExtents, BExtents, EExtents> &&
              __detail::__matrix_product_static_extents_v<AExtents, BExtents, CExtents> &&
              __detail::__compatible_static_extents_v<EExtents, CExtents>))
void matrix_product(
    ExecutionPolicy&& exec,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    std::mdspan<typename BAccessor::element_type, BExtents, BLayout, BAccessor> B,
    std::mdspan<typename EAccessor::element_type, EExtents, ELayout, EAccessor> E,
    std::mdspan<typename CAccessor::element_type, CExtents, CLayout, CAccessor> C)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::matrix_product(std::move(A), std::move(B), std::move(E), std::move(C));
    }();
}

// triangular_matrix_left_product — policy twin of level3.hpp.
template<class ExecutionPolicy, class Triangle, class DiagonalStorage,
         class AExtents, class ALayout, class AAccessor,
         class CExtents, class CLayout, class CAccessor>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__writable_output_v<CExtents, CLayout, CAccessor> &&
              __detail::__is_diagonal_storage_v<DiagonalStorage> &&
              __detail::__square_matrix_left_operand_static_extents_v<AExtents, CExtents> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle>))
void triangular_matrix_left_product(
    ExecutionPolicy&& exec,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle t,
    DiagonalStorage d,
    std::mdspan<typename CAccessor::element_type, CExtents, CLayout, CAccessor> C)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::triangular_matrix_left_product(std::move(A), std::move(t), std::move(d), std::move(C));
    }();
}

// triangular_matrix_right_product — policy twin of level3.hpp.
template<class ExecutionPolicy, class Triangle, class DiagonalStorage,
         class AExtents, class ALayout, class AAccessor,
         class CExtents, class CLayout, class CAccessor>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__writable_output_v<CExtents, CLayout, CAccessor> &&
              __detail::__is_diagonal_storage_v<DiagonalStorage> &&
              __detail::__square_matrix_right_operand_static_extents_v<AExtents, CExtents> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle>))
void triangular_matrix_right_product(
    ExecutionPolicy&& exec,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle t,
    DiagonalStorage d,
    std::mdspan<typename CAccessor::element_type, CExtents, CLayout, CAccessor> C)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::triangular_matrix_right_product(std::move(A), std::move(t), std::move(d), std::move(C));
    }();
}

// symmetric_matrix_product — policy twin of level3.hpp.
template<class ExecutionPolicy, class Triangle,
         class AExtents, class ALayout, class AAccessor,
         class BExtents, class BLayout, class BAccessor,
         class CExtents, class CLayout, class CAccessor>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__writable_output_v<CExtents, CLayout, CAccessor> &&
              __detail::__square_matrix_product_static_extents_v<AExtents, BExtents, CExtents> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle>))
void symmetric_matrix_product(
    ExecutionPolicy&& exec,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle t,
    std::mdspan<typename BAccessor::element_type, BExtents, BLayout, BAccessor> B,
    std::mdspan<typename CAccessor::element_type, CExtents, CLayout, CAccessor> C)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::symmetric_matrix_product(std::move(A), std::move(t), std::move(B), std::move(C));
    }();
}

// symmetric_matrix_rank_k_update — policy twin of level3.hpp.
template<class ExecutionPolicy, class ScalingFactor,
         class AExtents, class ALayout, class AAccessor,
         class CExtents, class CLayout, class CAccessor,
         class Triangle>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__scalar<ScalingFactor> &&
              __detail::__possibly_packed_output_v<CExtents, CLayout, CAccessor> &&
              __detail::__matrix_rank_k_static_extents_v<AExtents, CExtents> &&
              __detail::__packed_triangle_matches_v<CLayout, Triangle>))
void symmetric_matrix_rank_k_update(
    ExecutionPolicy&& exec,
    ScalingFactor alpha,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    std::mdspan<typename CAccessor::element_type, CExtents, CLayout, CAccessor> C,
    Triangle t)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::symmetric_matrix_rank_k_update(std::move(alpha), std::move(A), std::move(C), std::move(t));
    }();
}

// symmetric_matrix_rank_k_update — policy twin of level3.hpp.
template<class ExecutionPolicy, class ScalingFactor,
         class AExtents, class ALayout, class AAccessor,
         class EExtents, class ELayout, class EAccessor,
         class CExtents, class CLayout, class CAccessor,
         class Triangle>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__scalar<ScalingFactor> &&
              __detail::__possibly_packed_output_v<CExtents, CLayout, CAccessor> &&
              __detail::__matrix_rank_k_static_extents_v<AExtents, EExtents> &&
              __detail::__matrix_rank_k_static_extents_v<AExtents, CExtents> &&
              __detail::__compatible_static_extents_v<EExtents, CExtents> &&
              __detail::__packed_triangle_matches_v<ELayout, Triangle> &&
              __detail::__packed_triangle_matches_v<CLayout, Triangle>))
void symmetric_matrix_rank_k_update(
    ExecutionPolicy&& exec,
    ScalingFactor alpha,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    std::mdspan<typename EAccessor::element_type, EExtents, ELayout, EAccessor> E,
    std::mdspan<typename CAccessor::element_type, CExtents, CLayout, CAccessor> C,
    Triangle t)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::symmetric_matrix_rank_k_update(std::move(alpha), std::move(A), std::move(E), std::move(C), std::move(t));
    }();
}

// symmetric_matrix_rank_2k_update — policy twin of level3.hpp.
template<class ExecutionPolicy, class AExtents, class ALayout, class AAccessor,
         class BExtents, class BLayout, class BAccessor,
         class CExtents, class CLayout, class CAccessor,
         class Triangle>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__possibly_packed_output_v<CExtents, CLayout, CAccessor> &&
              __detail::__matrix_rank_2k_static_extents_v<AExtents, BExtents, CExtents> &&
              __detail::__packed_triangle_matches_v<CLayout, Triangle>))
void symmetric_matrix_rank_2k_update(
    ExecutionPolicy&& exec,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    std::mdspan<typename BAccessor::element_type, BExtents, BLayout, BAccessor> B,
    std::mdspan<typename CAccessor::element_type, CExtents, CLayout, CAccessor> C,
    Triangle t)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::symmetric_matrix_rank_2k_update(std::move(A), std::move(B), std::move(C), std::move(t));
    }();
}

// symmetric_matrix_rank_2k_update — policy twin of level3.hpp.
template<class ExecutionPolicy, class AExtents, class ALayout, class AAccessor,
         class BExtents, class BLayout, class BAccessor,
         class EExtents, class ELayout, class EAccessor,
         class CExtents, class CLayout, class CAccessor,
         class Triangle>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__possibly_packed_output_v<CExtents, CLayout, CAccessor> &&
              __detail::__matrix_rank_2k_static_extents_v<AExtents, BExtents, EExtents> &&
              __detail::__matrix_rank_2k_static_extents_v<AExtents, BExtents, CExtents> &&
              __detail::__compatible_static_extents_v<EExtents, CExtents> &&
              __detail::__packed_triangle_matches_v<ELayout, Triangle> &&
              __detail::__packed_triangle_matches_v<CLayout, Triangle>))
void symmetric_matrix_rank_2k_update(
    ExecutionPolicy&& exec,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    std::mdspan<typename BAccessor::element_type, BExtents, BLayout, BAccessor> B,
    std::mdspan<typename EAccessor::element_type, EExtents, ELayout, EAccessor> E,
    std::mdspan<typename CAccessor::element_type, CExtents, CLayout, CAccessor> C,
    Triangle t)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::symmetric_matrix_rank_2k_update(std::move(A), std::move(B), std::move(E), std::move(C), std::move(t));
    }();
}

// hermitian_matrix_product — policy twin of level3.hpp.
template<class ExecutionPolicy, class Triangle,
         class AExtents, class ALayout, class AAccessor,
         class BExtents, class BLayout, class BAccessor,
         class CExtents, class CLayout, class CAccessor>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__writable_output_v<CExtents, CLayout, CAccessor> &&
              __detail::__square_matrix_product_static_extents_v<AExtents, BExtents, CExtents> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle>))
void hermitian_matrix_product(
    ExecutionPolicy&& exec,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle t,
    std::mdspan<typename BAccessor::element_type, BExtents, BLayout, BAccessor> B,
    std::mdspan<typename CAccessor::element_type, CExtents, CLayout, CAccessor> C)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::hermitian_matrix_product(std::move(A), std::move(t), std::move(B), std::move(C));
    }();
}

// hermitian_matrix_rank_k_update — policy twin of level3.hpp.
template<class ExecutionPolicy, class ScalingFactor,
         class AExtents, class ALayout, class AAccessor,
         class CExtents, class CLayout, class CAccessor,
         class Triangle>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__scalar<ScalingFactor> &&
              __detail::__possibly_packed_output_v<CExtents, CLayout, CAccessor> &&
              __detail::__matrix_rank_k_static_extents_v<AExtents, CExtents> &&
              __detail::__packed_triangle_matches_v<CLayout, Triangle>))
void hermitian_matrix_rank_k_update(
    ExecutionPolicy&& exec,
    ScalingFactor alpha,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    std::mdspan<typename CAccessor::element_type, CExtents, CLayout, CAccessor> C,
    Triangle t)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::hermitian_matrix_rank_k_update(std::move(alpha), std::move(A), std::move(C), std::move(t));
    }();
}

// hermitian_matrix_rank_k_update — policy twin of level3.hpp.
template<class ExecutionPolicy, class ScalingFactor,
         class AExtents, class ALayout, class AAccessor,
         class EExtents, class ELayout, class EAccessor,
         class CExtents, class CLayout, class CAccessor,
         class Triangle>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__scalar<ScalingFactor> &&
              __detail::__possibly_packed_output_v<CExtents, CLayout, CAccessor> &&
              __detail::__matrix_rank_k_static_extents_v<AExtents, EExtents> &&
              __detail::__matrix_rank_k_static_extents_v<AExtents, CExtents> &&
              __detail::__compatible_static_extents_v<EExtents, CExtents> &&
              __detail::__packed_triangle_matches_v<ELayout, Triangle> &&
              __detail::__packed_triangle_matches_v<CLayout, Triangle>))
void hermitian_matrix_rank_k_update(
    ExecutionPolicy&& exec,
    ScalingFactor alpha,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    std::mdspan<typename EAccessor::element_type, EExtents, ELayout, EAccessor> E,
    std::mdspan<typename CAccessor::element_type, CExtents, CLayout, CAccessor> C,
    Triangle t)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::hermitian_matrix_rank_k_update(std::move(alpha), std::move(A), std::move(E), std::move(C), std::move(t));
    }();
}

// triangular_matrix_matrix_left_solve — policy twin of level3.hpp.
template<class ExecutionPolicy, class Triangle, class DiagonalStorage,
         class AExtents, class ALayout, class AAccessor,
         class BExtents, class BLayout, class BAccessor,
         class XExtents, class XLayout, class XAccessor,
         class BinaryDivideOp>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__writable_output_v<XExtents, XLayout, XAccessor> &&
              __detail::__is_diagonal_storage_v<DiagonalStorage> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle> &&
              __detail::__square_matrix_left_operand_static_extents_v<AExtents, BExtents> &&
              __detail::__square_matrix_left_operand_static_extents_v<AExtents, XExtents> &&
              __detail::__compatible_static_extents_v<BExtents, XExtents>))
void triangular_matrix_matrix_left_solve(
    ExecutionPolicy&& exec,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle t,
    DiagonalStorage d,
    std::mdspan<typename BAccessor::element_type, BExtents, BLayout, BAccessor> b,
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x,
    BinaryDivideOp divide)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::triangular_matrix_matrix_left_solve(std::move(A), std::move(t), std::move(d), std::move(b), std::move(x), std::move(divide));
    }();
}

// triangular_matrix_matrix_left_solve — policy twin of level3.hpp.
template<class ExecutionPolicy, class Triangle, class DiagonalStorage,
         class AExtents, class ALayout, class AAccessor,
         class BExtents, class BLayout, class BAccessor,
         class XExtents, class XLayout, class XAccessor>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__writable_output_v<XExtents, XLayout, XAccessor> &&
              __detail::__is_diagonal_storage_v<DiagonalStorage> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle> &&
              __detail::__square_matrix_left_operand_static_extents_v<AExtents, BExtents> &&
              __detail::__square_matrix_left_operand_static_extents_v<AExtents, XExtents> &&
              __detail::__compatible_static_extents_v<BExtents, XExtents>))
void triangular_matrix_matrix_left_solve(
    ExecutionPolicy&& exec,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle t,
    DiagonalStorage d,
    std::mdspan<typename BAccessor::element_type, BExtents, BLayout, BAccessor> b,
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::triangular_matrix_matrix_left_solve(std::move(A), std::move(t), std::move(d), std::move(b), std::move(x));
    }();
}

// triangular_matrix_matrix_left_solve — policy twin of level3.hpp.
template<class ExecutionPolicy, class Triangle, class DiagonalStorage,
         class AExtents, class ALayout, class AAccessor,
         class BExtents, class BLayout, class BAccessor,
         class BinaryDivideOp>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__writable_output_v<BExtents, BLayout, BAccessor> &&
              __detail::__is_diagonal_storage_v<DiagonalStorage> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle> &&
              __detail::__square_matrix_left_operand_static_extents_v<AExtents, BExtents>))
void triangular_matrix_matrix_left_solve(
    ExecutionPolicy&& exec,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle t,
    DiagonalStorage d,
    std::mdspan<typename BAccessor::element_type, BExtents, BLayout, BAccessor> b,
    BinaryDivideOp divide)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::triangular_matrix_matrix_left_solve(std::move(A), std::move(t), std::move(d), std::move(b), std::move(divide));
    }();
}

// triangular_matrix_matrix_left_solve — policy twin of level3.hpp.
template<class ExecutionPolicy, class Triangle, class DiagonalStorage,
         class AExtents, class ALayout, class AAccessor,
         class BExtents, class BLayout, class BAccessor>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__writable_output_v<BExtents, BLayout, BAccessor> &&
              __detail::__is_diagonal_storage_v<DiagonalStorage> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle> &&
              __detail::__square_matrix_left_operand_static_extents_v<AExtents, BExtents>))
void triangular_matrix_matrix_left_solve(
    ExecutionPolicy&& exec,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle t,
    DiagonalStorage d,
    std::mdspan<typename BAccessor::element_type, BExtents, BLayout, BAccessor> b)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::triangular_matrix_matrix_left_solve(std::move(A), std::move(t), std::move(d), std::move(b));
    }();
}

// triangular_matrix_matrix_right_solve — policy twin of level3.hpp.
template<class ExecutionPolicy, class Triangle, class DiagonalStorage,
         class AExtents, class ALayout, class AAccessor,
         class BExtents, class BLayout, class BAccessor,
         class XExtents, class XLayout, class XAccessor,
         class BinaryDivideOp>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__writable_output_v<XExtents, XLayout, XAccessor> &&
              __detail::__is_diagonal_storage_v<DiagonalStorage> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle> &&
              __detail::__square_matrix_right_operand_static_extents_v<AExtents, BExtents> &&
              __detail::__square_matrix_right_operand_static_extents_v<AExtents, XExtents> &&
              __detail::__compatible_static_extents_v<BExtents, XExtents>))
void triangular_matrix_matrix_right_solve(
    ExecutionPolicy&& exec,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle t,
    DiagonalStorage d,
    std::mdspan<typename BAccessor::element_type, BExtents, BLayout, BAccessor> b,
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x,
    BinaryDivideOp divide)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::triangular_matrix_matrix_right_solve(std::move(A), std::move(t), std::move(d), std::move(b), std::move(x), std::move(divide));
    }();
}

// triangular_matrix_matrix_right_solve — policy twin of level3.hpp.
template<class ExecutionPolicy, class Triangle, class DiagonalStorage,
         class AExtents, class ALayout, class AAccessor,
         class BExtents, class BLayout, class BAccessor,
         class XExtents, class XLayout, class XAccessor>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__writable_output_v<XExtents, XLayout, XAccessor> &&
              __detail::__is_diagonal_storage_v<DiagonalStorage> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle> &&
              __detail::__square_matrix_right_operand_static_extents_v<AExtents, BExtents> &&
              __detail::__square_matrix_right_operand_static_extents_v<AExtents, XExtents> &&
              __detail::__compatible_static_extents_v<BExtents, XExtents>))
void triangular_matrix_matrix_right_solve(
    ExecutionPolicy&& exec,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle t,
    DiagonalStorage d,
    std::mdspan<typename BAccessor::element_type, BExtents, BLayout, BAccessor> b,
    std::mdspan<typename XAccessor::element_type, XExtents, XLayout, XAccessor> x)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::triangular_matrix_matrix_right_solve(std::move(A), std::move(t), std::move(d), std::move(b), std::move(x));
    }();
}

// triangular_matrix_matrix_right_solve — policy twin of level3.hpp.
template<class ExecutionPolicy, class Triangle, class DiagonalStorage,
         class AExtents, class ALayout, class AAccessor,
         class BExtents, class BLayout, class BAccessor,
         class BinaryDivideOp>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__writable_output_v<BExtents, BLayout, BAccessor> &&
              __detail::__is_diagonal_storage_v<DiagonalStorage> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle> &&
              __detail::__square_matrix_right_operand_static_extents_v<AExtents, BExtents>))
void triangular_matrix_matrix_right_solve(
    ExecutionPolicy&& exec,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle t,
    DiagonalStorage d,
    std::mdspan<typename BAccessor::element_type, BExtents, BLayout, BAccessor> b,
    BinaryDivideOp divide)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::triangular_matrix_matrix_right_solve(std::move(A), std::move(t), std::move(d), std::move(b), std::move(divide));
    }();
}

// triangular_matrix_matrix_right_solve — policy twin of level3.hpp.
template<class ExecutionPolicy, class Triangle, class DiagonalStorage,
         class AExtents, class ALayout, class AAccessor,
         class BExtents, class BLayout, class BAccessor>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__writable_output_v<BExtents, BLayout, BAccessor> &&
              __detail::__is_diagonal_storage_v<DiagonalStorage> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle> &&
              __detail::__square_matrix_right_operand_static_extents_v<AExtents, BExtents>))
void triangular_matrix_matrix_right_solve(
    ExecutionPolicy&& exec,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle t,
    DiagonalStorage d,
    std::mdspan<typename BAccessor::element_type, BExtents, BLayout, BAccessor> b)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::triangular_matrix_matrix_right_solve(std::move(A), std::move(t), std::move(d), std::move(b));
    }();
}

// symmetric_matrix_product — policy twin of level3.hpp.
template<class ExecutionPolicy, class Triangle,
         class AExtents, class ALayout, class AAccessor,
         class BExtents, class BLayout, class BAccessor,
         class EExtents, class ELayout, class EAccessor,
         class CExtents, class CLayout, class CAccessor>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__writable_output_v<CExtents, CLayout, CAccessor> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle> &&
              __detail::__square_static_extents_v<AExtents> &&
              __detail::__matrix_product_static_extents_v<AExtents, BExtents, CExtents> &&
              __detail::__compatible_static_extents_v<EExtents, CExtents>))
void symmetric_matrix_product(
    ExecutionPolicy&& exec,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle t,
    std::mdspan<typename BAccessor::element_type, BExtents, BLayout, BAccessor> B,
    std::mdspan<typename EAccessor::element_type, EExtents, ELayout, EAccessor> E,
    std::mdspan<typename CAccessor::element_type, CExtents, CLayout, CAccessor> C)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::symmetric_matrix_product(std::move(A), std::move(t), std::move(B), std::move(E), std::move(C));
    }();
}

// symmetric_matrix_product — policy twin of level3.hpp.
template<class ExecutionPolicy, class Triangle,
         class AExtents, class ALayout, class AAccessor,
         class BExtents, class BLayout, class BAccessor,
         class CExtents, class CLayout, class CAccessor>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__writable_output_v<CExtents, CLayout, CAccessor> &&
              __detail::__packed_triangle_matches_v<BLayout, Triangle> &&
              __detail::__square_static_extents_v<BExtents> &&
              __detail::__matrix_product_static_extents_v<AExtents, BExtents, CExtents>))
void symmetric_matrix_product(
    ExecutionPolicy&& exec,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    std::mdspan<typename BAccessor::element_type, BExtents, BLayout, BAccessor> B,
    Triangle t,
    std::mdspan<typename CAccessor::element_type, CExtents, CLayout, CAccessor> C)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::symmetric_matrix_product(std::move(A), std::move(B), std::move(t), std::move(C));
    }();
}

// symmetric_matrix_product — policy twin of level3.hpp.
template<class ExecutionPolicy, class Triangle,
         class AExtents, class ALayout, class AAccessor,
         class BExtents, class BLayout, class BAccessor,
         class EExtents, class ELayout, class EAccessor,
         class CExtents, class CLayout, class CAccessor>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__writable_output_v<CExtents, CLayout, CAccessor> &&
              __detail::__packed_triangle_matches_v<BLayout, Triangle> &&
              __detail::__square_static_extents_v<BExtents> &&
              __detail::__matrix_product_static_extents_v<AExtents, BExtents, CExtents> &&
              __detail::__compatible_static_extents_v<EExtents, CExtents>))
void symmetric_matrix_product(
    ExecutionPolicy&& exec,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    std::mdspan<typename BAccessor::element_type, BExtents, BLayout, BAccessor> B,
    Triangle t,
    std::mdspan<typename EAccessor::element_type, EExtents, ELayout, EAccessor> E,
    std::mdspan<typename CAccessor::element_type, CExtents, CLayout, CAccessor> C)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::symmetric_matrix_product(std::move(A), std::move(B), std::move(t), std::move(E), std::move(C));
    }();
}

// hermitian_matrix_product — policy twin of level3.hpp.
template<class ExecutionPolicy, class Triangle,
         class AExtents, class ALayout, class AAccessor,
         class BExtents, class BLayout, class BAccessor,
         class EExtents, class ELayout, class EAccessor,
         class CExtents, class CLayout, class CAccessor>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__writable_output_v<CExtents, CLayout, CAccessor> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle> &&
              __detail::__square_static_extents_v<AExtents> &&
              __detail::__matrix_product_static_extents_v<AExtents, BExtents, CExtents> &&
              __detail::__compatible_static_extents_v<EExtents, CExtents>))
void hermitian_matrix_product(
    ExecutionPolicy&& exec,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle t,
    std::mdspan<typename BAccessor::element_type, BExtents, BLayout, BAccessor> B,
    std::mdspan<typename EAccessor::element_type, EExtents, ELayout, EAccessor> E,
    std::mdspan<typename CAccessor::element_type, CExtents, CLayout, CAccessor> C)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::hermitian_matrix_product(std::move(A), std::move(t), std::move(B), std::move(E), std::move(C));
    }();
}

// hermitian_matrix_product — policy twin of level3.hpp.
template<class ExecutionPolicy, class Triangle,
         class AExtents, class ALayout, class AAccessor,
         class BExtents, class BLayout, class BAccessor,
         class CExtents, class CLayout, class CAccessor>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__writable_output_v<CExtents, CLayout, CAccessor> &&
              __detail::__packed_triangle_matches_v<BLayout, Triangle> &&
              __detail::__square_static_extents_v<BExtents> &&
              __detail::__matrix_product_static_extents_v<AExtents, BExtents, CExtents>))
void hermitian_matrix_product(
    ExecutionPolicy&& exec,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    std::mdspan<typename BAccessor::element_type, BExtents, BLayout, BAccessor> B,
    Triangle t,
    std::mdspan<typename CAccessor::element_type, CExtents, CLayout, CAccessor> C)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::hermitian_matrix_product(std::move(A), std::move(B), std::move(t), std::move(C));
    }();
}

// hermitian_matrix_product — policy twin of level3.hpp.
template<class ExecutionPolicy, class Triangle,
         class AExtents, class ALayout, class AAccessor,
         class BExtents, class BLayout, class BAccessor,
         class EExtents, class ELayout, class EAccessor,
         class CExtents, class CLayout, class CAccessor>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__writable_output_v<CExtents, CLayout, CAccessor> &&
              __detail::__packed_triangle_matches_v<BLayout, Triangle> &&
              __detail::__square_static_extents_v<BExtents> &&
              __detail::__matrix_product_static_extents_v<AExtents, BExtents, CExtents> &&
              __detail::__compatible_static_extents_v<EExtents, CExtents>))
void hermitian_matrix_product(
    ExecutionPolicy&& exec,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    std::mdspan<typename BAccessor::element_type, BExtents, BLayout, BAccessor> B,
    Triangle t,
    std::mdspan<typename EAccessor::element_type, EExtents, ELayout, EAccessor> E,
    std::mdspan<typename CAccessor::element_type, CExtents, CLayout, CAccessor> C)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::hermitian_matrix_product(std::move(A), std::move(B), std::move(t), std::move(E), std::move(C));
    }();
}

// triangular_matrix_product — policy twin of level3.hpp.
template<class ExecutionPolicy, class Triangle, class DiagonalStorage,
         class AExtents, class ALayout, class AAccessor,
         class BExtents, class BLayout, class BAccessor,
         class CExtents, class CLayout, class CAccessor>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__writable_output_v<CExtents, CLayout, CAccessor> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle> &&
              __detail::__square_static_extents_v<AExtents> &&
              __detail::__matrix_product_static_extents_v<AExtents, BExtents, CExtents> &&
              __detail::__is_diagonal_storage_v<DiagonalStorage>))
void triangular_matrix_product(
    ExecutionPolicy&& exec,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle t,
    DiagonalStorage d,
    std::mdspan<typename BAccessor::element_type, BExtents, BLayout, BAccessor> B,
    std::mdspan<typename CAccessor::element_type, CExtents, CLayout, CAccessor> C)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::triangular_matrix_product(std::move(A), std::move(t), std::move(d), std::move(B), std::move(C));
    }();
}

// triangular_matrix_product — policy twin of level3.hpp.
template<class ExecutionPolicy, class Triangle, class DiagonalStorage,
         class AExtents, class ALayout, class AAccessor,
         class BExtents, class BLayout, class BAccessor,
         class EExtents, class ELayout, class EAccessor,
         class CExtents, class CLayout, class CAccessor>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__writable_output_v<CExtents, CLayout, CAccessor> &&
              __detail::__packed_triangle_matches_v<ALayout, Triangle> &&
              __detail::__square_static_extents_v<AExtents> &&
              __detail::__matrix_product_static_extents_v<AExtents, BExtents, CExtents> &&
              __detail::__is_diagonal_storage_v<DiagonalStorage> &&
              __detail::__compatible_static_extents_v<EExtents, CExtents>))
void triangular_matrix_product(
    ExecutionPolicy&& exec,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    Triangle t,
    DiagonalStorage d,
    std::mdspan<typename BAccessor::element_type, BExtents, BLayout, BAccessor> B,
    std::mdspan<typename EAccessor::element_type, EExtents, ELayout, EAccessor> E,
    std::mdspan<typename CAccessor::element_type, CExtents, CLayout, CAccessor> C)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::triangular_matrix_product(std::move(A), std::move(t), std::move(d), std::move(B), std::move(E), std::move(C));
    }();
}

// triangular_matrix_product — policy twin of level3.hpp.
template<class ExecutionPolicy, class Triangle, class DiagonalStorage,
         class AExtents, class ALayout, class AAccessor,
         class BExtents, class BLayout, class BAccessor,
         class CExtents, class CLayout, class CAccessor>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__writable_output_v<CExtents, CLayout, CAccessor> &&
              __detail::__packed_triangle_matches_v<BLayout, Triangle> &&
              __detail::__square_static_extents_v<BExtents> &&
              __detail::__matrix_product_static_extents_v<AExtents, BExtents, CExtents> &&
              __detail::__is_diagonal_storage_v<DiagonalStorage>))
void triangular_matrix_product(
    ExecutionPolicy&& exec,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    std::mdspan<typename BAccessor::element_type, BExtents, BLayout, BAccessor> B,
    Triangle t,
    DiagonalStorage d,
    std::mdspan<typename CAccessor::element_type, CExtents, CLayout, CAccessor> C)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::triangular_matrix_product(std::move(A), std::move(B), std::move(t), std::move(d), std::move(C));
    }();
}

// triangular_matrix_product — policy twin of level3.hpp.
template<class ExecutionPolicy, class Triangle, class DiagonalStorage,
         class AExtents, class ALayout, class AAccessor,
         class BExtents, class BLayout, class BAccessor,
         class EExtents, class ELayout, class EAccessor,
         class CExtents, class CLayout, class CAccessor>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__writable_output_v<CExtents, CLayout, CAccessor> &&
              __detail::__packed_triangle_matches_v<BLayout, Triangle> &&
              __detail::__square_static_extents_v<BExtents> &&
              __detail::__matrix_product_static_extents_v<AExtents, BExtents, CExtents> &&
              __detail::__is_diagonal_storage_v<DiagonalStorage> &&
              __detail::__compatible_static_extents_v<EExtents, CExtents>))
void triangular_matrix_product(
    ExecutionPolicy&& exec,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    std::mdspan<typename BAccessor::element_type, BExtents, BLayout, BAccessor> B,
    Triangle t,
    DiagonalStorage d,
    std::mdspan<typename EAccessor::element_type, EExtents, ELayout, EAccessor> E,
    std::mdspan<typename CAccessor::element_type, CExtents, CLayout, CAccessor> C)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::triangular_matrix_product(std::move(A), std::move(B), std::move(t), std::move(d), std::move(E), std::move(C));
    }();
}

// hermitian_matrix_rank_2k_update — policy twin of level3.hpp.
template<class ExecutionPolicy, class AExtents, class ALayout, class AAccessor,
         class BExtents, class BLayout, class BAccessor,
         class CExtents, class CLayout, class CAccessor,
         class Triangle>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__possibly_packed_output_v<CExtents, CLayout, CAccessor> &&
              __detail::__matrix_rank_2k_static_extents_v<AExtents, BExtents, CExtents> &&
              __detail::__packed_triangle_matches_v<CLayout, Triangle>))
void hermitian_matrix_rank_2k_update(
    ExecutionPolicy&& exec,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    std::mdspan<typename BAccessor::element_type, BExtents, BLayout, BAccessor> B,
    std::mdspan<typename CAccessor::element_type, CExtents, CLayout, CAccessor> C,
    Triangle t)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::hermitian_matrix_rank_2k_update(std::move(A), std::move(B), std::move(C), std::move(t));
    }();
}

// hermitian_matrix_rank_2k_update — policy twin of level3.hpp.
template<class ExecutionPolicy, class AExtents, class ALayout, class AAccessor,
         class BExtents, class BLayout, class BAccessor,
         class EExtents, class ELayout, class EAccessor,
         class CExtents, class CLayout, class CAccessor,
         class Triangle>
    requires (std::is_execution_policy_v<std::remove_cvref_t<ExecutionPolicy>> &&
              (__detail::__possibly_packed_output_v<CExtents, CLayout, CAccessor> &&
              __detail::__matrix_rank_2k_static_extents_v<AExtents, BExtents, CExtents> &&
              __detail::__packed_triangle_matches_v<CLayout, Triangle> &&
              __detail::__matrix_rank_2k_static_extents_v<AExtents, BExtents, EExtents> &&
              __detail::__compatible_static_extents_v<EExtents, CExtents> &&
              __detail::__packed_triangle_matches_v<ELayout, Triangle>))
void hermitian_matrix_rank_2k_update(
    ExecutionPolicy&& exec,
    std::mdspan<typename AAccessor::element_type, AExtents, ALayout, AAccessor> A,
    std::mdspan<typename BAccessor::element_type, BExtents, BLayout, BAccessor> B,
    std::mdspan<typename EAccessor::element_type, EExtents, ELayout, EAccessor> E,
    std::mdspan<typename CAccessor::element_type, CExtents, CLayout, CAccessor> C,
    Triangle t)
{
    (void)exec;
    return [&]() noexcept {
        return std::linalg::hermitian_matrix_rank_2k_update(std::move(A), std::move(B), std::move(E), std::move(C), std::move(t));
    }();
}

} // namespace std::linalg

#else
#define FORGE_LINALG_HAS_EXECUTION_POLICY_OVERLOADS 0
#endif
