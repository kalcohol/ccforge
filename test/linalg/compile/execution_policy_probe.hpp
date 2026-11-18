#pragma once
#include <linalg>
#include <complex>
#include <concepts>
#include <type_traits>
#include <utility>

#if defined(FORGE_TEST_LINALG_REQUIRE_POLICIES) && !FORGE_LINALG_HAS_EXECUTION_POLICY_OVERLOADS
#error "This target requires native execution policy objects"
#endif

#if FORGE_LINALG_HAS_EXECUTION_POLICY_OVERLOADS
namespace linalg_policy_probe {
template<class P>
consteval bool signatures() {
    using V = std::mdspan<double, std::extents<int, 1>>;
    using M = std::mdspan<double, std::extents<int, 1, 1>>;
    using Z = std::mdspan<std::complex<double>, std::extents<int, 1>>;
    static_assert(std::same_as<decltype(std::linalg::copy(std::declval<P>(), std::declval<V>(), std::declval<V>())),
                               decltype(std::linalg::copy(std::declval<V>(), std::declval<V>()))>);
    static_assert(!noexcept(std::linalg::copy(std::declval<P>(), std::declval<V>(), std::declval<V>())));
    static_assert(std::same_as<decltype(std::linalg::scale(std::declval<P>(), 3.0, std::declval<V>())),
                               decltype(std::linalg::scale(3.0, std::declval<V>()))>);
    static_assert(!noexcept(std::linalg::scale(std::declval<P>(), 3.0, std::declval<V>())));
    static_assert(std::same_as<decltype(std::linalg::swap_elements(std::declval<P>(), std::declval<V>(), std::declval<V>())),
                               decltype(std::linalg::swap_elements(std::declval<V>(), std::declval<V>()))>);
    static_assert(!noexcept(std::linalg::swap_elements(std::declval<P>(), std::declval<V>(), std::declval<V>())));
    static_assert(std::same_as<decltype(std::linalg::add(std::declval<P>(), std::declval<V>(), std::declval<V>(), std::declval<V>())),
                               decltype(std::linalg::add(std::declval<V>(), std::declval<V>(), std::declval<V>()))>);
    static_assert(!noexcept(std::linalg::add(std::declval<P>(), std::declval<V>(), std::declval<V>(), std::declval<V>())));
    static_assert(std::same_as<decltype(std::linalg::dot(std::declval<P>(), std::declval<V>(), std::declval<V>(), 3.0)),
                               decltype(std::linalg::dot(std::declval<V>(), std::declval<V>(), 3.0))>);
    static_assert(!noexcept(std::linalg::dot(std::declval<P>(), std::declval<V>(), std::declval<V>(), 3.0)));
    static_assert(std::same_as<decltype(std::linalg::dot(std::declval<P>(), std::declval<V>(), std::declval<V>())),
                               decltype(std::linalg::dot(std::declval<V>(), std::declval<V>()))>);
    static_assert(!noexcept(std::linalg::dot(std::declval<P>(), std::declval<V>(), std::declval<V>())));
    static_assert(std::same_as<decltype(std::linalg::dotc(std::declval<P>(), std::declval<V>(), std::declval<V>(), 3.0)),
                               decltype(std::linalg::dotc(std::declval<V>(), std::declval<V>(), 3.0))>);
    static_assert(!noexcept(std::linalg::dotc(std::declval<P>(), std::declval<V>(), std::declval<V>(), 3.0)));
    static_assert(std::same_as<decltype(std::linalg::dotc(std::declval<P>(), std::declval<V>(), std::declval<V>())),
                               decltype(std::linalg::dotc(std::declval<V>(), std::declval<V>()))>);
    static_assert(!noexcept(std::linalg::dotc(std::declval<P>(), std::declval<V>(), std::declval<V>())));
    static_assert(std::same_as<decltype(std::linalg::vector_two_norm(std::declval<P>(), std::declval<V>(), 3.0)),
                               decltype(std::linalg::vector_two_norm(std::declval<V>(), 3.0))>);
    static_assert(!noexcept(std::linalg::vector_two_norm(std::declval<P>(), std::declval<V>(), 3.0)));
    static_assert(std::same_as<decltype(std::linalg::vector_two_norm(std::declval<P>(), std::declval<V>())),
                               decltype(std::linalg::vector_two_norm(std::declval<V>()))>);
    static_assert(!noexcept(std::linalg::vector_two_norm(std::declval<P>(), std::declval<V>())));
    static_assert(std::same_as<decltype(std::linalg::vector_abs_sum(std::declval<P>(), std::declval<V>(), 3.0)),
                               decltype(std::linalg::vector_abs_sum(std::declval<V>(), 3.0))>);
    static_assert(!noexcept(std::linalg::vector_abs_sum(std::declval<P>(), std::declval<V>(), 3.0)));
    static_assert(std::same_as<decltype(std::linalg::vector_abs_sum(std::declval<P>(), std::declval<V>())),
                               decltype(std::linalg::vector_abs_sum(std::declval<V>()))>);
    static_assert(!noexcept(std::linalg::vector_abs_sum(std::declval<P>(), std::declval<V>())));
    static_assert(std::same_as<decltype(std::linalg::vector_idx_abs_max(std::declval<P>(), std::declval<V>())),
                               decltype(std::linalg::vector_idx_abs_max(std::declval<V>()))>);
    static_assert(!noexcept(std::linalg::vector_idx_abs_max(std::declval<P>(), std::declval<V>())));
    static_assert(std::same_as<decltype(std::linalg::apply_givens_rotation(std::declval<P>(), std::declval<V>(), std::declval<V>(), 0.0, 0.0)),
                               decltype(std::linalg::apply_givens_rotation(std::declval<V>(), std::declval<V>(), 0.0, 0.0))>);
    static_assert(!noexcept(std::linalg::apply_givens_rotation(std::declval<P>(), std::declval<V>(), std::declval<V>(), 0.0, 0.0)));
    static_assert(std::same_as<decltype(std::linalg::apply_givens_rotation(std::declval<P>(), std::declval<Z>(), std::declval<Z>(), 0.0, std::complex<double>{})),
                               decltype(std::linalg::apply_givens_rotation(std::declval<Z>(), std::declval<Z>(), 0.0, std::complex<double>{}))>);
    static_assert(!noexcept(std::linalg::apply_givens_rotation(std::declval<P>(), std::declval<Z>(), std::declval<Z>(), 0.0, std::complex<double>{})));
    static_assert(std::same_as<decltype(std::linalg::matrix_frob_norm(std::declval<P>(), std::declval<M>(), 3.0)),
                               decltype(std::linalg::matrix_frob_norm(std::declval<M>(), 3.0))>);
    static_assert(!noexcept(std::linalg::matrix_frob_norm(std::declval<P>(), std::declval<M>(), 3.0)));
    static_assert(std::same_as<decltype(std::linalg::matrix_frob_norm(std::declval<P>(), std::declval<M>())),
                               decltype(std::linalg::matrix_frob_norm(std::declval<M>()))>);
    static_assert(!noexcept(std::linalg::matrix_frob_norm(std::declval<P>(), std::declval<M>())));
    static_assert(std::same_as<decltype(std::linalg::matrix_one_norm(std::declval<P>(), std::declval<M>(), 3.0)),
                               decltype(std::linalg::matrix_one_norm(std::declval<M>(), 3.0))>);
    static_assert(!noexcept(std::linalg::matrix_one_norm(std::declval<P>(), std::declval<M>(), 3.0)));
    static_assert(std::same_as<decltype(std::linalg::matrix_one_norm(std::declval<P>(), std::declval<M>())),
                               decltype(std::linalg::matrix_one_norm(std::declval<M>()))>);
    static_assert(!noexcept(std::linalg::matrix_one_norm(std::declval<P>(), std::declval<M>())));
    static_assert(std::same_as<decltype(std::linalg::matrix_inf_norm(std::declval<P>(), std::declval<M>(), 3.0)),
                               decltype(std::linalg::matrix_inf_norm(std::declval<M>(), 3.0))>);
    static_assert(!noexcept(std::linalg::matrix_inf_norm(std::declval<P>(), std::declval<M>(), 3.0)));
    static_assert(std::same_as<decltype(std::linalg::matrix_inf_norm(std::declval<P>(), std::declval<M>())),
                               decltype(std::linalg::matrix_inf_norm(std::declval<M>()))>);
    static_assert(!noexcept(std::linalg::matrix_inf_norm(std::declval<P>(), std::declval<M>())));
    static_assert(std::same_as<decltype(std::linalg::matrix_vector_product(std::declval<P>(), std::declval<M>(), std::declval<V>(), std::declval<V>())),
                               decltype(std::linalg::matrix_vector_product(std::declval<M>(), std::declval<V>(), std::declval<V>()))>);
    static_assert(!noexcept(std::linalg::matrix_vector_product(std::declval<P>(), std::declval<M>(), std::declval<V>(), std::declval<V>())));
    static_assert(std::same_as<decltype(std::linalg::matrix_vector_product(std::declval<P>(), std::declval<M>(), std::declval<V>(), std::declval<V>(), std::declval<V>())),
                               decltype(std::linalg::matrix_vector_product(std::declval<M>(), std::declval<V>(), std::declval<V>(), std::declval<V>()))>);
    static_assert(!noexcept(std::linalg::matrix_vector_product(std::declval<P>(), std::declval<M>(), std::declval<V>(), std::declval<V>(), std::declval<V>())));
    static_assert(std::same_as<decltype(std::linalg::triangular_matrix_vector_product(std::declval<P>(), std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<V>())),
                               decltype(std::linalg::triangular_matrix_vector_product(std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<V>()))>);
    static_assert(!noexcept(std::linalg::triangular_matrix_vector_product(std::declval<P>(), std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<V>())));
    static_assert(std::same_as<decltype(std::linalg::triangular_matrix_vector_product(std::declval<P>(), std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<V>(), std::declval<V>())),
                               decltype(std::linalg::triangular_matrix_vector_product(std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<V>(), std::declval<V>()))>);
    static_assert(!noexcept(std::linalg::triangular_matrix_vector_product(std::declval<P>(), std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<V>(), std::declval<V>())));
    static_assert(std::same_as<decltype(std::linalg::symmetric_matrix_vector_product(std::declval<P>(), std::declval<M>(), std::linalg::upper_triangle, std::declval<V>(), std::declval<V>())),
                               decltype(std::linalg::symmetric_matrix_vector_product(std::declval<M>(), std::linalg::upper_triangle, std::declval<V>(), std::declval<V>()))>);
    static_assert(!noexcept(std::linalg::symmetric_matrix_vector_product(std::declval<P>(), std::declval<M>(), std::linalg::upper_triangle, std::declval<V>(), std::declval<V>())));
    static_assert(std::same_as<decltype(std::linalg::symmetric_matrix_vector_product(std::declval<P>(), std::declval<M>(), std::linalg::upper_triangle, std::declval<V>(), std::declval<V>(), std::declval<V>())),
                               decltype(std::linalg::symmetric_matrix_vector_product(std::declval<M>(), std::linalg::upper_triangle, std::declval<V>(), std::declval<V>(), std::declval<V>()))>);
    static_assert(!noexcept(std::linalg::symmetric_matrix_vector_product(std::declval<P>(), std::declval<M>(), std::linalg::upper_triangle, std::declval<V>(), std::declval<V>(), std::declval<V>())));
    static_assert(std::same_as<decltype(std::linalg::hermitian_matrix_vector_product(std::declval<P>(), std::declval<M>(), std::linalg::upper_triangle, std::declval<V>(), std::declval<V>())),
                               decltype(std::linalg::hermitian_matrix_vector_product(std::declval<M>(), std::linalg::upper_triangle, std::declval<V>(), std::declval<V>()))>);
    static_assert(!noexcept(std::linalg::hermitian_matrix_vector_product(std::declval<P>(), std::declval<M>(), std::linalg::upper_triangle, std::declval<V>(), std::declval<V>())));
    static_assert(std::same_as<decltype(std::linalg::hermitian_matrix_vector_product(std::declval<P>(), std::declval<M>(), std::linalg::upper_triangle, std::declval<V>(), std::declval<V>(), std::declval<V>())),
                               decltype(std::linalg::hermitian_matrix_vector_product(std::declval<M>(), std::linalg::upper_triangle, std::declval<V>(), std::declval<V>(), std::declval<V>()))>);
    static_assert(!noexcept(std::linalg::hermitian_matrix_vector_product(std::declval<P>(), std::declval<M>(), std::linalg::upper_triangle, std::declval<V>(), std::declval<V>(), std::declval<V>())));
    static_assert(std::same_as<decltype(std::linalg::matrix_rank_1_update(std::declval<P>(), std::declval<V>(), std::declval<V>(), std::declval<M>())),
                               decltype(std::linalg::matrix_rank_1_update(std::declval<V>(), std::declval<V>(), std::declval<M>()))>);
    static_assert(!noexcept(std::linalg::matrix_rank_1_update(std::declval<P>(), std::declval<V>(), std::declval<V>(), std::declval<M>())));
    static_assert(std::same_as<decltype(std::linalg::matrix_rank_1_update(std::declval<P>(), std::declval<V>(), std::declval<V>(), std::declval<M>(), std::declval<M>())),
                               decltype(std::linalg::matrix_rank_1_update(std::declval<V>(), std::declval<V>(), std::declval<M>(), std::declval<M>()))>);
    static_assert(!noexcept(std::linalg::matrix_rank_1_update(std::declval<P>(), std::declval<V>(), std::declval<V>(), std::declval<M>(), std::declval<M>())));
    static_assert(std::same_as<decltype(std::linalg::matrix_rank_1_update_c(std::declval<P>(), std::declval<V>(), std::declval<V>(), std::declval<M>())),
                               decltype(std::linalg::matrix_rank_1_update_c(std::declval<V>(), std::declval<V>(), std::declval<M>()))>);
    static_assert(!noexcept(std::linalg::matrix_rank_1_update_c(std::declval<P>(), std::declval<V>(), std::declval<V>(), std::declval<M>())));
    static_assert(std::same_as<decltype(std::linalg::matrix_rank_1_update_c(std::declval<P>(), std::declval<V>(), std::declval<V>(), std::declval<M>(), std::declval<M>())),
                               decltype(std::linalg::matrix_rank_1_update_c(std::declval<V>(), std::declval<V>(), std::declval<M>(), std::declval<M>()))>);
    static_assert(!noexcept(std::linalg::matrix_rank_1_update_c(std::declval<P>(), std::declval<V>(), std::declval<V>(), std::declval<M>(), std::declval<M>())));
    static_assert(std::same_as<decltype(std::linalg::symmetric_matrix_rank_1_update(std::declval<P>(), 3.0, std::declval<V>(), std::declval<M>(), std::linalg::upper_triangle)),
                               decltype(std::linalg::symmetric_matrix_rank_1_update(3.0, std::declval<V>(), std::declval<M>(), std::linalg::upper_triangle))>);
    static_assert(!noexcept(std::linalg::symmetric_matrix_rank_1_update(std::declval<P>(), 3.0, std::declval<V>(), std::declval<M>(), std::linalg::upper_triangle)));
    static_assert(std::same_as<decltype(std::linalg::symmetric_matrix_rank_1_update(std::declval<P>(), 3.0, std::declval<V>(), std::declval<M>(), std::declval<M>(), std::linalg::upper_triangle)),
                               decltype(std::linalg::symmetric_matrix_rank_1_update(3.0, std::declval<V>(), std::declval<M>(), std::declval<M>(), std::linalg::upper_triangle))>);
    static_assert(!noexcept(std::linalg::symmetric_matrix_rank_1_update(std::declval<P>(), 3.0, std::declval<V>(), std::declval<M>(), std::declval<M>(), std::linalg::upper_triangle)));
    static_assert(std::same_as<decltype(std::linalg::symmetric_matrix_rank_2_update(std::declval<P>(), std::declval<V>(), std::declval<V>(), std::declval<M>(), std::linalg::upper_triangle)),
                               decltype(std::linalg::symmetric_matrix_rank_2_update(std::declval<V>(), std::declval<V>(), std::declval<M>(), std::linalg::upper_triangle))>);
    static_assert(!noexcept(std::linalg::symmetric_matrix_rank_2_update(std::declval<P>(), std::declval<V>(), std::declval<V>(), std::declval<M>(), std::linalg::upper_triangle)));
    static_assert(std::same_as<decltype(std::linalg::symmetric_matrix_rank_2_update(std::declval<P>(), std::declval<V>(), std::declval<V>(), std::declval<M>(), std::declval<M>(), std::linalg::upper_triangle)),
                               decltype(std::linalg::symmetric_matrix_rank_2_update(std::declval<V>(), std::declval<V>(), std::declval<M>(), std::declval<M>(), std::linalg::upper_triangle))>);
    static_assert(!noexcept(std::linalg::symmetric_matrix_rank_2_update(std::declval<P>(), std::declval<V>(), std::declval<V>(), std::declval<M>(), std::declval<M>(), std::linalg::upper_triangle)));
    static_assert(std::same_as<decltype(std::linalg::hermitian_matrix_rank_1_update(std::declval<P>(), 3.0, std::declval<V>(), std::declval<M>(), std::linalg::upper_triangle)),
                               decltype(std::linalg::hermitian_matrix_rank_1_update(3.0, std::declval<V>(), std::declval<M>(), std::linalg::upper_triangle))>);
    static_assert(!noexcept(std::linalg::hermitian_matrix_rank_1_update(std::declval<P>(), 3.0, std::declval<V>(), std::declval<M>(), std::linalg::upper_triangle)));
    static_assert(std::same_as<decltype(std::linalg::hermitian_matrix_rank_1_update(std::declval<P>(), 3.0, std::declval<V>(), std::declval<M>(), std::declval<M>(), std::linalg::upper_triangle)),
                               decltype(std::linalg::hermitian_matrix_rank_1_update(3.0, std::declval<V>(), std::declval<M>(), std::declval<M>(), std::linalg::upper_triangle))>);
    static_assert(!noexcept(std::linalg::hermitian_matrix_rank_1_update(std::declval<P>(), 3.0, std::declval<V>(), std::declval<M>(), std::declval<M>(), std::linalg::upper_triangle)));
    static_assert(std::same_as<decltype(std::linalg::hermitian_matrix_rank_2_update(std::declval<P>(), std::declval<V>(), std::declval<V>(), std::declval<M>(), std::linalg::upper_triangle)),
                               decltype(std::linalg::hermitian_matrix_rank_2_update(std::declval<V>(), std::declval<V>(), std::declval<M>(), std::linalg::upper_triangle))>);
    static_assert(!noexcept(std::linalg::hermitian_matrix_rank_2_update(std::declval<P>(), std::declval<V>(), std::declval<V>(), std::declval<M>(), std::linalg::upper_triangle)));
    static_assert(std::same_as<decltype(std::linalg::hermitian_matrix_rank_2_update(std::declval<P>(), std::declval<V>(), std::declval<V>(), std::declval<M>(), std::declval<M>(), std::linalg::upper_triangle)),
                               decltype(std::linalg::hermitian_matrix_rank_2_update(std::declval<V>(), std::declval<V>(), std::declval<M>(), std::declval<M>(), std::linalg::upper_triangle))>);
    static_assert(!noexcept(std::linalg::hermitian_matrix_rank_2_update(std::declval<P>(), std::declval<V>(), std::declval<V>(), std::declval<M>(), std::declval<M>(), std::linalg::upper_triangle)));
    static_assert(std::same_as<decltype(std::linalg::triangular_matrix_vector_solve(std::declval<P>(), std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<V>(), std::declval<V>(), std::divides<void>{})),
                               decltype(std::linalg::triangular_matrix_vector_solve(std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<V>(), std::declval<V>(), std::divides<void>{}))>);
    static_assert(!noexcept(std::linalg::triangular_matrix_vector_solve(std::declval<P>(), std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<V>(), std::declval<V>(), std::divides<void>{})));
    static_assert(std::same_as<decltype(std::linalg::triangular_matrix_vector_solve(std::declval<P>(), std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<V>(), std::declval<V>())),
                               decltype(std::linalg::triangular_matrix_vector_solve(std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<V>(), std::declval<V>()))>);
    static_assert(!noexcept(std::linalg::triangular_matrix_vector_solve(std::declval<P>(), std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<V>(), std::declval<V>())));
    static_assert(std::same_as<decltype(std::linalg::triangular_matrix_vector_solve(std::declval<P>(), std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<V>(), std::divides<void>{})),
                               decltype(std::linalg::triangular_matrix_vector_solve(std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<V>(), std::divides<void>{}))>);
    static_assert(!noexcept(std::linalg::triangular_matrix_vector_solve(std::declval<P>(), std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<V>(), std::divides<void>{})));
    static_assert(std::same_as<decltype(std::linalg::triangular_matrix_vector_solve(std::declval<P>(), std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<V>())),
                               decltype(std::linalg::triangular_matrix_vector_solve(std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<V>()))>);
    static_assert(!noexcept(std::linalg::triangular_matrix_vector_solve(std::declval<P>(), std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<V>())));
    static_assert(std::same_as<decltype(std::linalg::triangular_matrix_vector_product(std::declval<P>(), std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<V>(), std::declval<V>(), std::declval<V>())),
                               decltype(std::linalg::triangular_matrix_vector_product(std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<V>(), std::declval<V>(), std::declval<V>()))>);
    static_assert(!noexcept(std::linalg::triangular_matrix_vector_product(std::declval<P>(), std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<V>(), std::declval<V>(), std::declval<V>())));
    static_assert(std::same_as<decltype(std::linalg::matrix_product(std::declval<P>(), std::declval<M>(), std::declval<M>(), std::declval<M>())),
                               decltype(std::linalg::matrix_product(std::declval<M>(), std::declval<M>(), std::declval<M>()))>);
    static_assert(!noexcept(std::linalg::matrix_product(std::declval<P>(), std::declval<M>(), std::declval<M>(), std::declval<M>())));
    static_assert(std::same_as<decltype(std::linalg::matrix_product(std::declval<P>(), std::declval<M>(), std::declval<M>(), std::declval<M>(), std::declval<M>())),
                               decltype(std::linalg::matrix_product(std::declval<M>(), std::declval<M>(), std::declval<M>(), std::declval<M>()))>);
    static_assert(!noexcept(std::linalg::matrix_product(std::declval<P>(), std::declval<M>(), std::declval<M>(), std::declval<M>(), std::declval<M>())));
    static_assert(std::same_as<decltype(std::linalg::triangular_matrix_left_product(std::declval<P>(), std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<M>())),
                               decltype(std::linalg::triangular_matrix_left_product(std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<M>()))>);
    static_assert(!noexcept(std::linalg::triangular_matrix_left_product(std::declval<P>(), std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<M>())));
    static_assert(std::same_as<decltype(std::linalg::triangular_matrix_right_product(std::declval<P>(), std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<M>())),
                               decltype(std::linalg::triangular_matrix_right_product(std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<M>()))>);
    static_assert(!noexcept(std::linalg::triangular_matrix_right_product(std::declval<P>(), std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<M>())));
    static_assert(std::same_as<decltype(std::linalg::symmetric_matrix_product(std::declval<P>(), std::declval<M>(), std::linalg::upper_triangle, std::declval<M>(), std::declval<M>())),
                               decltype(std::linalg::symmetric_matrix_product(std::declval<M>(), std::linalg::upper_triangle, std::declval<M>(), std::declval<M>()))>);
    static_assert(!noexcept(std::linalg::symmetric_matrix_product(std::declval<P>(), std::declval<M>(), std::linalg::upper_triangle, std::declval<M>(), std::declval<M>())));
    static_assert(std::same_as<decltype(std::linalg::symmetric_matrix_rank_k_update(std::declval<P>(), 3.0, std::declval<M>(), std::declval<M>(), std::linalg::upper_triangle)),
                               decltype(std::linalg::symmetric_matrix_rank_k_update(3.0, std::declval<M>(), std::declval<M>(), std::linalg::upper_triangle))>);
    static_assert(!noexcept(std::linalg::symmetric_matrix_rank_k_update(std::declval<P>(), 3.0, std::declval<M>(), std::declval<M>(), std::linalg::upper_triangle)));
    static_assert(std::same_as<decltype(std::linalg::symmetric_matrix_rank_k_update(std::declval<P>(), 3.0, std::declval<M>(), std::declval<M>(), std::declval<M>(), std::linalg::upper_triangle)),
                               decltype(std::linalg::symmetric_matrix_rank_k_update(3.0, std::declval<M>(), std::declval<M>(), std::declval<M>(), std::linalg::upper_triangle))>);
    static_assert(!noexcept(std::linalg::symmetric_matrix_rank_k_update(std::declval<P>(), 3.0, std::declval<M>(), std::declval<M>(), std::declval<M>(), std::linalg::upper_triangle)));
    static_assert(std::same_as<decltype(std::linalg::symmetric_matrix_rank_2k_update(std::declval<P>(), std::declval<M>(), std::declval<M>(), std::declval<M>(), std::linalg::upper_triangle)),
                               decltype(std::linalg::symmetric_matrix_rank_2k_update(std::declval<M>(), std::declval<M>(), std::declval<M>(), std::linalg::upper_triangle))>);
    static_assert(!noexcept(std::linalg::symmetric_matrix_rank_2k_update(std::declval<P>(), std::declval<M>(), std::declval<M>(), std::declval<M>(), std::linalg::upper_triangle)));
    static_assert(std::same_as<decltype(std::linalg::symmetric_matrix_rank_2k_update(std::declval<P>(), std::declval<M>(), std::declval<M>(), std::declval<M>(), std::declval<M>(), std::linalg::upper_triangle)),
                               decltype(std::linalg::symmetric_matrix_rank_2k_update(std::declval<M>(), std::declval<M>(), std::declval<M>(), std::declval<M>(), std::linalg::upper_triangle))>);
    static_assert(!noexcept(std::linalg::symmetric_matrix_rank_2k_update(std::declval<P>(), std::declval<M>(), std::declval<M>(), std::declval<M>(), std::declval<M>(), std::linalg::upper_triangle)));
    static_assert(std::same_as<decltype(std::linalg::hermitian_matrix_product(std::declval<P>(), std::declval<M>(), std::linalg::upper_triangle, std::declval<M>(), std::declval<M>())),
                               decltype(std::linalg::hermitian_matrix_product(std::declval<M>(), std::linalg::upper_triangle, std::declval<M>(), std::declval<M>()))>);
    static_assert(!noexcept(std::linalg::hermitian_matrix_product(std::declval<P>(), std::declval<M>(), std::linalg::upper_triangle, std::declval<M>(), std::declval<M>())));
    static_assert(std::same_as<decltype(std::linalg::hermitian_matrix_rank_k_update(std::declval<P>(), 3.0, std::declval<M>(), std::declval<M>(), std::linalg::upper_triangle)),
                               decltype(std::linalg::hermitian_matrix_rank_k_update(3.0, std::declval<M>(), std::declval<M>(), std::linalg::upper_triangle))>);
    static_assert(!noexcept(std::linalg::hermitian_matrix_rank_k_update(std::declval<P>(), 3.0, std::declval<M>(), std::declval<M>(), std::linalg::upper_triangle)));
    static_assert(std::same_as<decltype(std::linalg::hermitian_matrix_rank_k_update(std::declval<P>(), 3.0, std::declval<M>(), std::declval<M>(), std::declval<M>(), std::linalg::upper_triangle)),
                               decltype(std::linalg::hermitian_matrix_rank_k_update(3.0, std::declval<M>(), std::declval<M>(), std::declval<M>(), std::linalg::upper_triangle))>);
    static_assert(!noexcept(std::linalg::hermitian_matrix_rank_k_update(std::declval<P>(), 3.0, std::declval<M>(), std::declval<M>(), std::declval<M>(), std::linalg::upper_triangle)));
    static_assert(std::same_as<decltype(std::linalg::triangular_matrix_matrix_left_solve(std::declval<P>(), std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<M>(), std::declval<M>(), std::divides<void>{})),
                               decltype(std::linalg::triangular_matrix_matrix_left_solve(std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<M>(), std::declval<M>(), std::divides<void>{}))>);
    static_assert(!noexcept(std::linalg::triangular_matrix_matrix_left_solve(std::declval<P>(), std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<M>(), std::declval<M>(), std::divides<void>{})));
    static_assert(std::same_as<decltype(std::linalg::triangular_matrix_matrix_left_solve(std::declval<P>(), std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<M>(), std::declval<M>())),
                               decltype(std::linalg::triangular_matrix_matrix_left_solve(std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<M>(), std::declval<M>()))>);
    static_assert(!noexcept(std::linalg::triangular_matrix_matrix_left_solve(std::declval<P>(), std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<M>(), std::declval<M>())));
    static_assert(std::same_as<decltype(std::linalg::triangular_matrix_matrix_left_solve(std::declval<P>(), std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<M>(), std::divides<void>{})),
                               decltype(std::linalg::triangular_matrix_matrix_left_solve(std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<M>(), std::divides<void>{}))>);
    static_assert(!noexcept(std::linalg::triangular_matrix_matrix_left_solve(std::declval<P>(), std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<M>(), std::divides<void>{})));
    static_assert(std::same_as<decltype(std::linalg::triangular_matrix_matrix_left_solve(std::declval<P>(), std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<M>())),
                               decltype(std::linalg::triangular_matrix_matrix_left_solve(std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<M>()))>);
    static_assert(!noexcept(std::linalg::triangular_matrix_matrix_left_solve(std::declval<P>(), std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<M>())));
    static_assert(std::same_as<decltype(std::linalg::triangular_matrix_matrix_right_solve(std::declval<P>(), std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<M>(), std::declval<M>(), std::divides<void>{})),
                               decltype(std::linalg::triangular_matrix_matrix_right_solve(std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<M>(), std::declval<M>(), std::divides<void>{}))>);
    static_assert(!noexcept(std::linalg::triangular_matrix_matrix_right_solve(std::declval<P>(), std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<M>(), std::declval<M>(), std::divides<void>{})));
    static_assert(std::same_as<decltype(std::linalg::triangular_matrix_matrix_right_solve(std::declval<P>(), std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<M>(), std::declval<M>())),
                               decltype(std::linalg::triangular_matrix_matrix_right_solve(std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<M>(), std::declval<M>()))>);
    static_assert(!noexcept(std::linalg::triangular_matrix_matrix_right_solve(std::declval<P>(), std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<M>(), std::declval<M>())));
    static_assert(std::same_as<decltype(std::linalg::triangular_matrix_matrix_right_solve(std::declval<P>(), std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<M>(), std::divides<void>{})),
                               decltype(std::linalg::triangular_matrix_matrix_right_solve(std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<M>(), std::divides<void>{}))>);
    static_assert(!noexcept(std::linalg::triangular_matrix_matrix_right_solve(std::declval<P>(), std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<M>(), std::divides<void>{})));
    static_assert(std::same_as<decltype(std::linalg::triangular_matrix_matrix_right_solve(std::declval<P>(), std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<M>())),
                               decltype(std::linalg::triangular_matrix_matrix_right_solve(std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<M>()))>);
    static_assert(!noexcept(std::linalg::triangular_matrix_matrix_right_solve(std::declval<P>(), std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<M>())));
    static_assert(std::same_as<decltype(std::linalg::symmetric_matrix_product(std::declval<P>(), std::declval<M>(), std::linalg::upper_triangle, std::declval<M>(), std::declval<M>(), std::declval<M>())),
                               decltype(std::linalg::symmetric_matrix_product(std::declval<M>(), std::linalg::upper_triangle, std::declval<M>(), std::declval<M>(), std::declval<M>()))>);
    static_assert(!noexcept(std::linalg::symmetric_matrix_product(std::declval<P>(), std::declval<M>(), std::linalg::upper_triangle, std::declval<M>(), std::declval<M>(), std::declval<M>())));
    static_assert(std::same_as<decltype(std::linalg::symmetric_matrix_product(std::declval<P>(), std::declval<M>(), std::declval<M>(), std::linalg::upper_triangle, std::declval<M>())),
                               decltype(std::linalg::symmetric_matrix_product(std::declval<M>(), std::declval<M>(), std::linalg::upper_triangle, std::declval<M>()))>);
    static_assert(!noexcept(std::linalg::symmetric_matrix_product(std::declval<P>(), std::declval<M>(), std::declval<M>(), std::linalg::upper_triangle, std::declval<M>())));
    static_assert(std::same_as<decltype(std::linalg::symmetric_matrix_product(std::declval<P>(), std::declval<M>(), std::declval<M>(), std::linalg::upper_triangle, std::declval<M>(), std::declval<M>())),
                               decltype(std::linalg::symmetric_matrix_product(std::declval<M>(), std::declval<M>(), std::linalg::upper_triangle, std::declval<M>(), std::declval<M>()))>);
    static_assert(!noexcept(std::linalg::symmetric_matrix_product(std::declval<P>(), std::declval<M>(), std::declval<M>(), std::linalg::upper_triangle, std::declval<M>(), std::declval<M>())));
    static_assert(std::same_as<decltype(std::linalg::hermitian_matrix_product(std::declval<P>(), std::declval<M>(), std::linalg::upper_triangle, std::declval<M>(), std::declval<M>(), std::declval<M>())),
                               decltype(std::linalg::hermitian_matrix_product(std::declval<M>(), std::linalg::upper_triangle, std::declval<M>(), std::declval<M>(), std::declval<M>()))>);
    static_assert(!noexcept(std::linalg::hermitian_matrix_product(std::declval<P>(), std::declval<M>(), std::linalg::upper_triangle, std::declval<M>(), std::declval<M>(), std::declval<M>())));
    static_assert(std::same_as<decltype(std::linalg::hermitian_matrix_product(std::declval<P>(), std::declval<M>(), std::declval<M>(), std::linalg::upper_triangle, std::declval<M>())),
                               decltype(std::linalg::hermitian_matrix_product(std::declval<M>(), std::declval<M>(), std::linalg::upper_triangle, std::declval<M>()))>);
    static_assert(!noexcept(std::linalg::hermitian_matrix_product(std::declval<P>(), std::declval<M>(), std::declval<M>(), std::linalg::upper_triangle, std::declval<M>())));
    static_assert(std::same_as<decltype(std::linalg::hermitian_matrix_product(std::declval<P>(), std::declval<M>(), std::declval<M>(), std::linalg::upper_triangle, std::declval<M>(), std::declval<M>())),
                               decltype(std::linalg::hermitian_matrix_product(std::declval<M>(), std::declval<M>(), std::linalg::upper_triangle, std::declval<M>(), std::declval<M>()))>);
    static_assert(!noexcept(std::linalg::hermitian_matrix_product(std::declval<P>(), std::declval<M>(), std::declval<M>(), std::linalg::upper_triangle, std::declval<M>(), std::declval<M>())));
    static_assert(std::same_as<decltype(std::linalg::triangular_matrix_product(std::declval<P>(), std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<M>(), std::declval<M>())),
                               decltype(std::linalg::triangular_matrix_product(std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<M>(), std::declval<M>()))>);
    static_assert(!noexcept(std::linalg::triangular_matrix_product(std::declval<P>(), std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<M>(), std::declval<M>())));
    static_assert(std::same_as<decltype(std::linalg::triangular_matrix_product(std::declval<P>(), std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<M>(), std::declval<M>(), std::declval<M>())),
                               decltype(std::linalg::triangular_matrix_product(std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<M>(), std::declval<M>(), std::declval<M>()))>);
    static_assert(!noexcept(std::linalg::triangular_matrix_product(std::declval<P>(), std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<M>(), std::declval<M>(), std::declval<M>())));
    static_assert(std::same_as<decltype(std::linalg::triangular_matrix_product(std::declval<P>(), std::declval<M>(), std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<M>())),
                               decltype(std::linalg::triangular_matrix_product(std::declval<M>(), std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<M>()))>);
    static_assert(!noexcept(std::linalg::triangular_matrix_product(std::declval<P>(), std::declval<M>(), std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<M>())));
    static_assert(std::same_as<decltype(std::linalg::triangular_matrix_product(std::declval<P>(), std::declval<M>(), std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<M>(), std::declval<M>())),
                               decltype(std::linalg::triangular_matrix_product(std::declval<M>(), std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<M>(), std::declval<M>()))>);
    static_assert(!noexcept(std::linalg::triangular_matrix_product(std::declval<P>(), std::declval<M>(), std::declval<M>(), std::linalg::upper_triangle, std::linalg::explicit_diagonal, std::declval<M>(), std::declval<M>())));
    static_assert(std::same_as<decltype(std::linalg::hermitian_matrix_rank_2k_update(std::declval<P>(), std::declval<M>(), std::declval<M>(), std::declval<M>(), std::linalg::upper_triangle)),
                               decltype(std::linalg::hermitian_matrix_rank_2k_update(std::declval<M>(), std::declval<M>(), std::declval<M>(), std::linalg::upper_triangle))>);
    static_assert(!noexcept(std::linalg::hermitian_matrix_rank_2k_update(std::declval<P>(), std::declval<M>(), std::declval<M>(), std::declval<M>(), std::linalg::upper_triangle)));
    static_assert(std::same_as<decltype(std::linalg::hermitian_matrix_rank_2k_update(std::declval<P>(), std::declval<M>(), std::declval<M>(), std::declval<M>(), std::declval<M>(), std::linalg::upper_triangle)),
                               decltype(std::linalg::hermitian_matrix_rank_2k_update(std::declval<M>(), std::declval<M>(), std::declval<M>(), std::declval<M>(), std::linalg::upper_triangle))>);
    static_assert(!noexcept(std::linalg::hermitian_matrix_rank_2k_update(std::declval<P>(), std::declval<M>(), std::declval<M>(), std::declval<M>(), std::declval<M>(), std::linalg::upper_triangle)));
    return true;
}
using P = std::execution::sequenced_policy;
static_assert(signatures<P&>());
static_assert(signatures<const P&>());
static_assert(signatures<P>());
static_assert(signatures<const P>());
static_assert(signatures<std::execution::parallel_policy>());
static_assert(signatures<std::execution::parallel_unsequenced_policy>());
#if __cpp_lib_execution >= 201902L
static_assert(signatures<std::execution::unsequenced_policy>());
#endif

template<class Policy, class V>
concept policy_copy = requires(Policy p, V v) { std::linalg::copy(p, v, v); };

using V = std::mdspan<double, std::extents<int, 1>>;
static_assert(!policy_copy<int, V>);
static_assert(!policy_copy<V, V>);
struct sequenced_policy {};
static_assert(!policy_copy<sequenced_policy, V>);
} // namespace linalg_policy_probe
#endif
