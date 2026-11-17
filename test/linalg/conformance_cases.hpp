#pragma once

#include <linalg>
#include <gtest/gtest.h>
#include <complex>
#include <cmath>
#include <utility>

namespace linalg_conformance {

// The same independent scalar cases instantiate both public overload sets.
// These expectations are arithmetic constants, never calls to a twin.
struct sequential_calls {
    template<class... Args>
    decltype(auto) copy(Args&&... args) const {
        return std::linalg::copy(std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) scale(Args&&... args) const {
        return std::linalg::scale(std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) swap_elements(Args&&... args) const {
        return std::linalg::swap_elements(std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) add(Args&&... args) const {
        return std::linalg::add(std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) dot(Args&&... args) const {
        return std::linalg::dot(std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) dotc(Args&&... args) const {
        return std::linalg::dotc(std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) vector_two_norm(Args&&... args) const {
        return std::linalg::vector_two_norm(std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) vector_abs_sum(Args&&... args) const {
        return std::linalg::vector_abs_sum(std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) vector_idx_abs_max(Args&&... args) const {
        return std::linalg::vector_idx_abs_max(std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) apply_givens_rotation(Args&&... args) const {
        return std::linalg::apply_givens_rotation(std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) matrix_frob_norm(Args&&... args) const {
        return std::linalg::matrix_frob_norm(std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) matrix_one_norm(Args&&... args) const {
        return std::linalg::matrix_one_norm(std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) matrix_inf_norm(Args&&... args) const {
        return std::linalg::matrix_inf_norm(std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) matrix_vector_product(Args&&... args) const {
        return std::linalg::matrix_vector_product(std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) triangular_matrix_vector_product(Args&&... args) const {
        return std::linalg::triangular_matrix_vector_product(std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) symmetric_matrix_vector_product(Args&&... args) const {
        return std::linalg::symmetric_matrix_vector_product(std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) hermitian_matrix_vector_product(Args&&... args) const {
        return std::linalg::hermitian_matrix_vector_product(std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) matrix_rank_1_update(Args&&... args) const {
        return std::linalg::matrix_rank_1_update(std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) matrix_rank_1_update_c(Args&&... args) const {
        return std::linalg::matrix_rank_1_update_c(std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) symmetric_matrix_rank_1_update(Args&&... args) const {
        return std::linalg::symmetric_matrix_rank_1_update(std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) symmetric_matrix_rank_2_update(Args&&... args) const {
        return std::linalg::symmetric_matrix_rank_2_update(std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) hermitian_matrix_rank_1_update(Args&&... args) const {
        return std::linalg::hermitian_matrix_rank_1_update(std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) hermitian_matrix_rank_2_update(Args&&... args) const {
        return std::linalg::hermitian_matrix_rank_2_update(std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) triangular_matrix_vector_solve(Args&&... args) const {
        return std::linalg::triangular_matrix_vector_solve(std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) matrix_product(Args&&... args) const {
        return std::linalg::matrix_product(std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) triangular_matrix_left_product(Args&&... args) const {
        return std::linalg::triangular_matrix_left_product(std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) triangular_matrix_right_product(Args&&... args) const {
        return std::linalg::triangular_matrix_right_product(std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) symmetric_matrix_product(Args&&... args) const {
        return std::linalg::symmetric_matrix_product(std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) symmetric_matrix_rank_k_update(Args&&... args) const {
        return std::linalg::symmetric_matrix_rank_k_update(std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) symmetric_matrix_rank_2k_update(Args&&... args) const {
        return std::linalg::symmetric_matrix_rank_2k_update(std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) hermitian_matrix_product(Args&&... args) const {
        return std::linalg::hermitian_matrix_product(std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) hermitian_matrix_rank_k_update(Args&&... args) const {
        return std::linalg::hermitian_matrix_rank_k_update(std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) triangular_matrix_matrix_left_solve(Args&&... args) const {
        return std::linalg::triangular_matrix_matrix_left_solve(std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) triangular_matrix_matrix_right_solve(Args&&... args) const {
        return std::linalg::triangular_matrix_matrix_right_solve(std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) triangular_matrix_product(Args&&... args) const {
        return std::linalg::triangular_matrix_product(std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) hermitian_matrix_rank_2k_update(Args&&... args) const {
        return std::linalg::hermitian_matrix_rank_2k_update(std::forward<Args>(args)...);
    }
};

#if FORGE_LINALG_HAS_EXECUTION_POLICY_OVERLOADS
template<class Policy>
struct policy_calls {
    const Policy& policy;
    template<class... Args>
    decltype(auto) copy(Args&&... args) const {
        return std::linalg::copy(policy, std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) scale(Args&&... args) const {
        return std::linalg::scale(policy, std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) swap_elements(Args&&... args) const {
        return std::linalg::swap_elements(policy, std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) add(Args&&... args) const {
        return std::linalg::add(policy, std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) dot(Args&&... args) const {
        return std::linalg::dot(policy, std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) dotc(Args&&... args) const {
        return std::linalg::dotc(policy, std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) vector_two_norm(Args&&... args) const {
        return std::linalg::vector_two_norm(policy, std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) vector_abs_sum(Args&&... args) const {
        return std::linalg::vector_abs_sum(policy, std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) vector_idx_abs_max(Args&&... args) const {
        return std::linalg::vector_idx_abs_max(policy, std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) apply_givens_rotation(Args&&... args) const {
        return std::linalg::apply_givens_rotation(policy, std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) matrix_frob_norm(Args&&... args) const {
        return std::linalg::matrix_frob_norm(policy, std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) matrix_one_norm(Args&&... args) const {
        return std::linalg::matrix_one_norm(policy, std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) matrix_inf_norm(Args&&... args) const {
        return std::linalg::matrix_inf_norm(policy, std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) matrix_vector_product(Args&&... args) const {
        return std::linalg::matrix_vector_product(policy, std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) triangular_matrix_vector_product(Args&&... args) const {
        return std::linalg::triangular_matrix_vector_product(policy, std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) symmetric_matrix_vector_product(Args&&... args) const {
        return std::linalg::symmetric_matrix_vector_product(policy, std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) hermitian_matrix_vector_product(Args&&... args) const {
        return std::linalg::hermitian_matrix_vector_product(policy, std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) matrix_rank_1_update(Args&&... args) const {
        return std::linalg::matrix_rank_1_update(policy, std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) matrix_rank_1_update_c(Args&&... args) const {
        return std::linalg::matrix_rank_1_update_c(policy, std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) symmetric_matrix_rank_1_update(Args&&... args) const {
        return std::linalg::symmetric_matrix_rank_1_update(policy, std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) symmetric_matrix_rank_2_update(Args&&... args) const {
        return std::linalg::symmetric_matrix_rank_2_update(policy, std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) hermitian_matrix_rank_1_update(Args&&... args) const {
        return std::linalg::hermitian_matrix_rank_1_update(policy, std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) hermitian_matrix_rank_2_update(Args&&... args) const {
        return std::linalg::hermitian_matrix_rank_2_update(policy, std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) triangular_matrix_vector_solve(Args&&... args) const {
        return std::linalg::triangular_matrix_vector_solve(policy, std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) matrix_product(Args&&... args) const {
        return std::linalg::matrix_product(policy, std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) triangular_matrix_left_product(Args&&... args) const {
        return std::linalg::triangular_matrix_left_product(policy, std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) triangular_matrix_right_product(Args&&... args) const {
        return std::linalg::triangular_matrix_right_product(policy, std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) symmetric_matrix_product(Args&&... args) const {
        return std::linalg::symmetric_matrix_product(policy, std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) symmetric_matrix_rank_k_update(Args&&... args) const {
        return std::linalg::symmetric_matrix_rank_k_update(policy, std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) symmetric_matrix_rank_2k_update(Args&&... args) const {
        return std::linalg::symmetric_matrix_rank_2k_update(policy, std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) hermitian_matrix_product(Args&&... args) const {
        return std::linalg::hermitian_matrix_product(policy, std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) hermitian_matrix_rank_k_update(Args&&... args) const {
        return std::linalg::hermitian_matrix_rank_k_update(policy, std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) triangular_matrix_matrix_left_solve(Args&&... args) const {
        return std::linalg::triangular_matrix_matrix_left_solve(policy, std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) triangular_matrix_matrix_right_solve(Args&&... args) const {
        return std::linalg::triangular_matrix_matrix_right_solve(policy, std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) triangular_matrix_product(Args&&... args) const {
        return std::linalg::triangular_matrix_product(policy, std::forward<Args>(args)...);
    }
    template<class... Args>
    decltype(auto) hermitian_matrix_rank_2k_update(Args&&... args) const {
        return std::linalg::hermitian_matrix_rank_2k_update(policy, std::forward<Args>(args)...);
    }
};
#endif

template<class Calls>
void scalar_cases(const Calls& calls) {
    { // 1: copy(from, to)
        SCOPED_TRACE("1: copy");
        double xd[]{2.0};
        double yd[]{3.0};
        std::mdspan x(xd, std::extents<int, 1>{});
        std::mdspan y(yd, std::extents<int, 1>{});
        calls.copy(x, y);
        EXPECT_EQ(yd[0], 2.0);
    }
    { // 2: scale(alpha, x)
        SCOPED_TRACE("2: scale");
        double xd[]{2.0};
        std::mdspan x(xd, std::extents<int, 1>{});
        calls.scale(3.0, x);
        EXPECT_EQ(xd[0], 6.0);
    }
    { // 3: swap_elements(x, y)
        SCOPED_TRACE("3: swap_elements");
        double xd[]{2.0};
        double yd[]{3.0};
        std::mdspan x(xd, std::extents<int, 1>{});
        std::mdspan y(yd, std::extents<int, 1>{});
        calls.swap_elements(x, y);
        EXPECT_EQ(yd[0], 2.0);
        EXPECT_EQ(xd[0], 3.0);
    }
    { // 4: add(x, y, z)
        SCOPED_TRACE("4: add");
        double xd[]{2.0};
        double yd[]{3.0};
        double zd[]{0.0};
        std::mdspan x(xd, std::extents<int, 1>{});
        std::mdspan y(yd, std::extents<int, 1>{});
        std::mdspan z(zd, std::extents<int, 1>{});
        calls.add(x, y, z);
        EXPECT_EQ(zd[0], 5.0);
    }
    { // 5: dot(x, y, init)
        SCOPED_TRACE("5: dot");
        double xd[]{2.0};
        double yd[]{3.0};
        std::mdspan x(xd, std::extents<int, 1>{});
        std::mdspan y(yd, std::extents<int, 1>{});
        EXPECT_EQ(calls.dot(x, y, 7.0), 13.0);
    }
    { // 6: dot(x, y)
        SCOPED_TRACE("6: dot");
        double xd[]{2.0};
        double yd[]{3.0};
        std::mdspan x(xd, std::extents<int, 1>{});
        std::mdspan y(yd, std::extents<int, 1>{});
        EXPECT_EQ(calls.dot(x, y), 6.0);
    }
    { // 7: dotc(x, y, init)
        SCOPED_TRACE("7: dotc");
        double xd[]{2.0};
        double yd[]{3.0};
        std::mdspan x(xd, std::extents<int, 1>{});
        std::mdspan y(yd, std::extents<int, 1>{});
        EXPECT_EQ(calls.dotc(x, y, 7.0), 13.0);
    }
    { // 8: dotc(x, y)
        SCOPED_TRACE("8: dotc");
        double xd[]{2.0};
        double yd[]{3.0};
        std::mdspan x(xd, std::extents<int, 1>{});
        std::mdspan y(yd, std::extents<int, 1>{});
        EXPECT_EQ(calls.dotc(x, y), 6.0);
    }
    { // 9: vector_two_norm(x, init)
        SCOPED_TRACE("9: vector_two_norm");
        double xd[]{2.0};
        std::mdspan x(xd, std::extents<int, 1>{});
        EXPECT_NEAR(calls.vector_two_norm(x, 7.0), std::sqrt(53.0), 1e-12);
    }
    { // 10: vector_two_norm(x)
        SCOPED_TRACE("10: vector_two_norm");
        double xd[]{2.0};
        std::mdspan x(xd, std::extents<int, 1>{});
        EXPECT_EQ(calls.vector_two_norm(x), 2.0);
    }
    { // 11: vector_abs_sum(x, init)
        SCOPED_TRACE("11: vector_abs_sum");
        double xd[]{2.0};
        std::mdspan x(xd, std::extents<int, 1>{});
        EXPECT_EQ(calls.vector_abs_sum(x, 7.0), 9.0);
    }
    { // 12: vector_abs_sum(x)
        SCOPED_TRACE("12: vector_abs_sum");
        double xd[]{2.0};
        std::mdspan x(xd, std::extents<int, 1>{});
        EXPECT_EQ(calls.vector_abs_sum(x), 2.0);
    }
    { // 13: vector_idx_abs_max(x)
        SCOPED_TRACE("13: vector_idx_abs_max");
        double xd[]{2.0};
        std::mdspan x(xd, std::extents<int, 1>{});
        EXPECT_EQ(calls.vector_idx_abs_max(x), 0u);
    }
    { // 14: apply_givens_rotation(x, y, c, s)
        SCOPED_TRACE("14: apply_givens_rotation");
        double xd[]{2.0};
        double yd[]{3.0};
        std::mdspan x(xd, std::extents<int, 1>{});
        std::mdspan y(yd, std::extents<int, 1>{});
        calls.apply_givens_rotation(x, y, 0.0, 1.0);
        EXPECT_EQ(yd[0], -2.0);
        EXPECT_EQ(xd[0], 3.0);
    }
    { // 15: apply_givens_rotation(x, y, c, s)
        SCOPED_TRACE("15: apply_givens_rotation");
        using value = std::complex<double>;
        value xd[]{2.0}, yd[]{3.0};
        std::mdspan x(xd, std::extents<int, 1>{});
        std::mdspan y(yd, std::extents<int, 1>{});
        calls.apply_givens_rotation(x, y, 0.0, value{0.0, 1.0});
        EXPECT_EQ(xd[0], value(0.0, 3.0));
        EXPECT_EQ(yd[0], value(0.0, 2.0));
    }
    { // 16: matrix_frob_norm(x, init)
        SCOPED_TRACE("16: matrix_frob_norm");
        double ad[]{2.0};
        double xd[]{2.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        std::mdspan x(xd, std::extents<int, 1>{});
        EXPECT_NEAR(calls.matrix_frob_norm(A, 7.0), std::sqrt(53.0), 1e-12);
    }
    { // 17: matrix_frob_norm(x)
        SCOPED_TRACE("17: matrix_frob_norm");
        double ad[]{2.0};
        double xd[]{2.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        std::mdspan x(xd, std::extents<int, 1>{});
        EXPECT_EQ(calls.matrix_frob_norm(A), 2.0);
    }
    { // 18: matrix_one_norm(A, init)
        SCOPED_TRACE("18: matrix_one_norm");
        double ad[]{2.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        EXPECT_EQ(calls.matrix_one_norm(A, 7.0), 9.0);
    }
    { // 19: matrix_one_norm(A)
        SCOPED_TRACE("19: matrix_one_norm");
        double ad[]{2.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        EXPECT_EQ(calls.matrix_one_norm(A), 2.0);
    }
    { // 20: matrix_inf_norm(A, init)
        SCOPED_TRACE("20: matrix_inf_norm");
        double ad[]{2.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        EXPECT_EQ(calls.matrix_inf_norm(A, 7.0), 9.0);
    }
    { // 21: matrix_inf_norm(A)
        SCOPED_TRACE("21: matrix_inf_norm");
        double ad[]{2.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        EXPECT_EQ(calls.matrix_inf_norm(A), 2.0);
    }
    { // 22: matrix_vector_product(A, x, y)
        SCOPED_TRACE("22: matrix_vector_product");
        double ad[]{2.0};
        double xd[]{2.0};
        double yd[]{3.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        std::mdspan x(xd, std::extents<int, 1>{});
        std::mdspan y(yd, std::extents<int, 1>{});
        calls.matrix_vector_product(A, x, y);
        EXPECT_EQ(yd[0], 4.0);
    }
    { // 23: matrix_vector_product(A, x, y, z)
        SCOPED_TRACE("23: matrix_vector_product");
        double ad[]{2.0};
        double xd[]{2.0};
        double yd[]{3.0};
        double zd[]{0.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        std::mdspan x(xd, std::extents<int, 1>{});
        std::mdspan y(yd, std::extents<int, 1>{});
        std::mdspan z(zd, std::extents<int, 1>{});
        calls.matrix_vector_product(A, x, y, z);
        EXPECT_EQ(zd[0], 7.0);
    }
    { // 24: triangular_matrix_vector_product(A, t, d, x)
        SCOPED_TRACE("24: triangular_matrix_vector_product");
        double ad[]{2.0};
        double xd[]{2.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        std::mdspan x(xd, std::extents<int, 1>{});
        calls.triangular_matrix_vector_product(A, std::linalg::upper_triangle, std::linalg::explicit_diagonal, x);
        EXPECT_EQ(xd[0], 4.0);
    }
    { // 25: triangular_matrix_vector_product(A, t, d, x, y)
        SCOPED_TRACE("25: triangular_matrix_vector_product");
        double ad[]{2.0};
        double xd[]{2.0};
        double yd[]{3.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        std::mdspan x(xd, std::extents<int, 1>{});
        std::mdspan y(yd, std::extents<int, 1>{});
        calls.triangular_matrix_vector_product(A, std::linalg::upper_triangle, std::linalg::explicit_diagonal, x, y);
        EXPECT_EQ(yd[0], 4.0);
    }
    { // 26: symmetric_matrix_vector_product(A, t, x, y)
        SCOPED_TRACE("26: symmetric_matrix_vector_product");
        double ad[]{2.0};
        double xd[]{2.0};
        double yd[]{3.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        std::mdspan x(xd, std::extents<int, 1>{});
        std::mdspan y(yd, std::extents<int, 1>{});
        calls.symmetric_matrix_vector_product(A, std::linalg::upper_triangle, x, y);
        EXPECT_EQ(yd[0], 4.0);
    }
    { // 27: symmetric_matrix_vector_product(A, t, x, y, z)
        SCOPED_TRACE("27: symmetric_matrix_vector_product");
        double ad[]{2.0};
        double xd[]{2.0};
        double yd[]{3.0};
        double zd[]{0.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        std::mdspan x(xd, std::extents<int, 1>{});
        std::mdspan y(yd, std::extents<int, 1>{});
        std::mdspan z(zd, std::extents<int, 1>{});
        calls.symmetric_matrix_vector_product(A, std::linalg::upper_triangle, x, y, z);
        EXPECT_EQ(zd[0], 7.0);
    }
    { // 28: hermitian_matrix_vector_product(A, t, x, y)
        SCOPED_TRACE("28: hermitian_matrix_vector_product");
        double ad[]{2.0};
        double xd[]{2.0};
        double yd[]{3.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        std::mdspan x(xd, std::extents<int, 1>{});
        std::mdspan y(yd, std::extents<int, 1>{});
        calls.hermitian_matrix_vector_product(A, std::linalg::upper_triangle, x, y);
        EXPECT_EQ(yd[0], 4.0);
    }
    { // 29: hermitian_matrix_vector_product(A, t, x, y, z)
        SCOPED_TRACE("29: hermitian_matrix_vector_product");
        double ad[]{2.0};
        double xd[]{2.0};
        double yd[]{3.0};
        double zd[]{0.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        std::mdspan x(xd, std::extents<int, 1>{});
        std::mdspan y(yd, std::extents<int, 1>{});
        std::mdspan z(zd, std::extents<int, 1>{});
        calls.hermitian_matrix_vector_product(A, std::linalg::upper_triangle, x, y, z);
        EXPECT_EQ(zd[0], 7.0);
    }
    { // 30: matrix_rank_1_update(x, y, A)
        SCOPED_TRACE("30: matrix_rank_1_update");
        double ad[]{2.0};
        double xd[]{2.0};
        double yd[]{3.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        std::mdspan x(xd, std::extents<int, 1>{});
        std::mdspan y(yd, std::extents<int, 1>{});
        calls.matrix_rank_1_update(x, y, A);
        EXPECT_EQ(ad[0], 6.0);
    }
    { // 31: matrix_rank_1_update(x, y, E, A)
        SCOPED_TRACE("31: matrix_rank_1_update");
        double ad[]{2.0};
        double ed[]{5.0};
        double xd[]{2.0};
        double yd[]{3.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        std::mdspan E(ed, std::extents<int, 1, 1>{});
        std::mdspan x(xd, std::extents<int, 1>{});
        std::mdspan y(yd, std::extents<int, 1>{});
        calls.matrix_rank_1_update(x, y, E, A);
        EXPECT_EQ(ad[0], 11.0);
    }
    { // 32: matrix_rank_1_update_c(x, y, A)
        SCOPED_TRACE("32: matrix_rank_1_update_c");
        double ad[]{2.0};
        double xd[]{2.0};
        double yd[]{3.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        std::mdspan x(xd, std::extents<int, 1>{});
        std::mdspan y(yd, std::extents<int, 1>{});
        calls.matrix_rank_1_update_c(x, y, A);
        EXPECT_EQ(ad[0], 6.0);
    }
    { // 33: matrix_rank_1_update_c(x, y, E, A)
        SCOPED_TRACE("33: matrix_rank_1_update_c");
        double ad[]{2.0};
        double ed[]{5.0};
        double xd[]{2.0};
        double yd[]{3.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        std::mdspan E(ed, std::extents<int, 1, 1>{});
        std::mdspan x(xd, std::extents<int, 1>{});
        std::mdspan y(yd, std::extents<int, 1>{});
        calls.matrix_rank_1_update_c(x, y, E, A);
        EXPECT_EQ(ad[0], 11.0);
    }
    { // 34: symmetric_matrix_rank_1_update(alpha, x, A, t)
        SCOPED_TRACE("34: symmetric_matrix_rank_1_update");
        double ad[]{2.0};
        double xd[]{2.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        std::mdspan x(xd, std::extents<int, 1>{});
        calls.symmetric_matrix_rank_1_update(3.0, x, A, std::linalg::upper_triangle);
        EXPECT_EQ(ad[0], 12.0);
    }
    { // 35: symmetric_matrix_rank_1_update(alpha, x, E, A, t)
        SCOPED_TRACE("35: symmetric_matrix_rank_1_update");
        double ad[]{2.0};
        double ed[]{5.0};
        double xd[]{2.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        std::mdspan E(ed, std::extents<int, 1, 1>{});
        std::mdspan x(xd, std::extents<int, 1>{});
        calls.symmetric_matrix_rank_1_update(3.0, x, E, A, std::linalg::upper_triangle);
        EXPECT_EQ(ad[0], 17.0);
    }
    { // 36: symmetric_matrix_rank_2_update(x, y, A, t)
        SCOPED_TRACE("36: symmetric_matrix_rank_2_update");
        double ad[]{2.0};
        double xd[]{2.0};
        double yd[]{3.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        std::mdspan x(xd, std::extents<int, 1>{});
        std::mdspan y(yd, std::extents<int, 1>{});
        calls.symmetric_matrix_rank_2_update(x, y, A, std::linalg::upper_triangle);
        EXPECT_EQ(ad[0], 12.0);
    }
    { // 37: symmetric_matrix_rank_2_update(x, y, E, A, t)
        SCOPED_TRACE("37: symmetric_matrix_rank_2_update");
        double ad[]{2.0};
        double ed[]{5.0};
        double xd[]{2.0};
        double yd[]{3.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        std::mdspan E(ed, std::extents<int, 1, 1>{});
        std::mdspan x(xd, std::extents<int, 1>{});
        std::mdspan y(yd, std::extents<int, 1>{});
        calls.symmetric_matrix_rank_2_update(x, y, E, A, std::linalg::upper_triangle);
        EXPECT_EQ(ad[0], 17.0);
    }
    { // 38: hermitian_matrix_rank_1_update(alpha, x, A, t)
        SCOPED_TRACE("38: hermitian_matrix_rank_1_update");
        double ad[]{2.0};
        double xd[]{2.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        std::mdspan x(xd, std::extents<int, 1>{});
        calls.hermitian_matrix_rank_1_update(3.0, x, A, std::linalg::upper_triangle);
        EXPECT_EQ(ad[0], 12.0);
    }
    { // 39: hermitian_matrix_rank_1_update(alpha, x, E, A, t)
        SCOPED_TRACE("39: hermitian_matrix_rank_1_update");
        double ad[]{2.0};
        double ed[]{5.0};
        double xd[]{2.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        std::mdspan E(ed, std::extents<int, 1, 1>{});
        std::mdspan x(xd, std::extents<int, 1>{});
        calls.hermitian_matrix_rank_1_update(3.0, x, E, A, std::linalg::upper_triangle);
        EXPECT_EQ(ad[0], 17.0);
    }
    { // 40: hermitian_matrix_rank_2_update(x, y, A, t)
        SCOPED_TRACE("40: hermitian_matrix_rank_2_update");
        double ad[]{2.0};
        double xd[]{2.0};
        double yd[]{3.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        std::mdspan x(xd, std::extents<int, 1>{});
        std::mdspan y(yd, std::extents<int, 1>{});
        calls.hermitian_matrix_rank_2_update(x, y, A, std::linalg::upper_triangle);
        EXPECT_EQ(ad[0], 12.0);
    }
    { // 41: hermitian_matrix_rank_2_update(x, y, E, A, t)
        SCOPED_TRACE("41: hermitian_matrix_rank_2_update");
        double ad[]{2.0};
        double ed[]{5.0};
        double xd[]{2.0};
        double yd[]{3.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        std::mdspan E(ed, std::extents<int, 1, 1>{});
        std::mdspan x(xd, std::extents<int, 1>{});
        std::mdspan y(yd, std::extents<int, 1>{});
        calls.hermitian_matrix_rank_2_update(x, y, E, A, std::linalg::upper_triangle);
        EXPECT_EQ(ad[0], 17.0);
    }
    { // 42: triangular_matrix_vector_solve(A, t, d, b, x, divide)
        SCOPED_TRACE("42: triangular_matrix_vector_solve");
        double ad[]{2.0};
        double xd[]{2.0};
        double rhs[]{6.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        std::mdspan x(xd, std::extents<int, 1>{});
        std::mdspan b(rhs, std::extents<int, 1>{});
        auto divide = [](double numerator, double denominator) {
            EXPECT_EQ(denominator, 2.0);
            return numerator / denominator;
        };
        calls.triangular_matrix_vector_solve(A, std::linalg::upper_triangle, std::linalg::explicit_diagonal, b, x, divide);
        EXPECT_EQ(xd[0], 3.0);
    }
    { // 43: triangular_matrix_vector_solve(A, t, d, b, x)
        SCOPED_TRACE("43: triangular_matrix_vector_solve");
        double ad[]{2.0};
        double xd[]{2.0};
        double rhs[]{6.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        std::mdspan x(xd, std::extents<int, 1>{});
        std::mdspan b(rhs, std::extents<int, 1>{});
        calls.triangular_matrix_vector_solve(A, std::linalg::upper_triangle, std::linalg::explicit_diagonal, b, x);
        EXPECT_EQ(xd[0], 3.0);
    }
    { // 44: triangular_matrix_vector_solve(A, t, d, b, divide)
        SCOPED_TRACE("44: triangular_matrix_vector_solve");
        double ad[]{2.0};
        double rhs[]{6.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        std::mdspan b(rhs, std::extents<int, 1>{});
        auto divide = [](double numerator, double denominator) {
            EXPECT_EQ(denominator, 2.0);
            return numerator / denominator;
        };
        calls.triangular_matrix_vector_solve(A, std::linalg::upper_triangle, std::linalg::explicit_diagonal, b, divide);
        EXPECT_EQ(rhs[0], 3.0);
    }
    { // 45: triangular_matrix_vector_solve(A, t, d, b)
        SCOPED_TRACE("45: triangular_matrix_vector_solve");
        double ad[]{2.0};
        double rhs[]{6.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        std::mdspan b(rhs, std::extents<int, 1>{});
        calls.triangular_matrix_vector_solve(A, std::linalg::upper_triangle, std::linalg::explicit_diagonal, b);
        EXPECT_EQ(rhs[0], 3.0);
    }
    { // 46: triangular_matrix_vector_product(A, t, d, x, y, z)
        SCOPED_TRACE("46: triangular_matrix_vector_product");
        double ad[]{2.0};
        double xd[]{2.0};
        double yd[]{3.0};
        double zd[]{0.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        std::mdspan x(xd, std::extents<int, 1>{});
        std::mdspan y(yd, std::extents<int, 1>{});
        std::mdspan z(zd, std::extents<int, 1>{});
        calls.triangular_matrix_vector_product(A, std::linalg::upper_triangle, std::linalg::explicit_diagonal, x, y, z);
        EXPECT_EQ(zd[0], 7.0);
    }
    { // 47: matrix_product(A, B, C)
        SCOPED_TRACE("47: matrix_product");
        double ad[]{2.0};
        double bd[]{3.0};
        double cd[]{0.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        std::mdspan B(bd, std::extents<int, 1, 1>{});
        std::mdspan C(cd, std::extents<int, 1, 1>{});
        calls.matrix_product(A, B, C);
        EXPECT_EQ(cd[0], 6.0);
    }
    { // 48: matrix_product(A, B, E, C)
        SCOPED_TRACE("48: matrix_product");
        double ad[]{2.0};
        double bd[]{3.0};
        double cd[]{0.0};
        double ed[]{5.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        std::mdspan B(bd, std::extents<int, 1, 1>{});
        std::mdspan C(cd, std::extents<int, 1, 1>{});
        std::mdspan E(ed, std::extents<int, 1, 1>{});
        calls.matrix_product(A, B, E, C);
        EXPECT_EQ(cd[0], 11.0);
    }
    { // 49: triangular_matrix_left_product(A, t, d, C)
        SCOPED_TRACE("49: triangular_matrix_left_product");
        double ad[]{2.0};
        double cd[]{0.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        std::mdspan C(cd, std::extents<int, 1, 1>{});
        cd[0] = 3.0;
        calls.triangular_matrix_left_product(A, std::linalg::upper_triangle, std::linalg::explicit_diagonal, C);
        EXPECT_EQ(cd[0], 6.0);
    }
    { // 50: triangular_matrix_right_product(A, t, d, C)
        SCOPED_TRACE("50: triangular_matrix_right_product");
        double ad[]{2.0};
        double cd[]{0.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        std::mdspan C(cd, std::extents<int, 1, 1>{});
        cd[0] = 3.0;
        calls.triangular_matrix_right_product(A, std::linalg::upper_triangle, std::linalg::explicit_diagonal, C);
        EXPECT_EQ(cd[0], 6.0);
    }
    { // 51: symmetric_matrix_product(A, t, B, C)
        SCOPED_TRACE("51: symmetric_matrix_product");
        double ad[]{2.0};
        double bd[]{3.0};
        double cd[]{0.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        std::mdspan B(bd, std::extents<int, 1, 1>{});
        std::mdspan C(cd, std::extents<int, 1, 1>{});
        calls.symmetric_matrix_product(A, std::linalg::upper_triangle, B, C);
        EXPECT_EQ(cd[0], 6.0);
    }
    { // 52: symmetric_matrix_rank_k_update(alpha, A, C, t)
        SCOPED_TRACE("52: symmetric_matrix_rank_k_update");
        double ad[]{2.0};
        double cd[]{0.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        std::mdspan C(cd, std::extents<int, 1, 1>{});
        calls.symmetric_matrix_rank_k_update(3.0, A, C, std::linalg::upper_triangle);
        EXPECT_EQ(cd[0], 12.0);
    }
    { // 53: symmetric_matrix_rank_k_update(alpha, A, E, C, t)
        SCOPED_TRACE("53: symmetric_matrix_rank_k_update");
        double ad[]{2.0};
        double cd[]{0.0};
        double ed[]{5.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        std::mdspan C(cd, std::extents<int, 1, 1>{});
        std::mdspan E(ed, std::extents<int, 1, 1>{});
        calls.symmetric_matrix_rank_k_update(3.0, A, E, C, std::linalg::upper_triangle);
        EXPECT_EQ(cd[0], 17.0);
    }
    { // 54: symmetric_matrix_rank_2k_update(A, B, C, t)
        SCOPED_TRACE("54: symmetric_matrix_rank_2k_update");
        double ad[]{2.0};
        double bd[]{3.0};
        double cd[]{0.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        std::mdspan B(bd, std::extents<int, 1, 1>{});
        std::mdspan C(cd, std::extents<int, 1, 1>{});
        calls.symmetric_matrix_rank_2k_update(A, B, C, std::linalg::upper_triangle);
        EXPECT_EQ(cd[0], 12.0);
    }
    { // 55: symmetric_matrix_rank_2k_update(A, B, E, C, t)
        SCOPED_TRACE("55: symmetric_matrix_rank_2k_update");
        double ad[]{2.0};
        double bd[]{3.0};
        double cd[]{0.0};
        double ed[]{5.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        std::mdspan B(bd, std::extents<int, 1, 1>{});
        std::mdspan C(cd, std::extents<int, 1, 1>{});
        std::mdspan E(ed, std::extents<int, 1, 1>{});
        calls.symmetric_matrix_rank_2k_update(A, B, E, C, std::linalg::upper_triangle);
        EXPECT_EQ(cd[0], 17.0);
    }
    { // 56: hermitian_matrix_product(A, t, B, C)
        SCOPED_TRACE("56: hermitian_matrix_product");
        double ad[]{2.0};
        double bd[]{3.0};
        double cd[]{0.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        std::mdspan B(bd, std::extents<int, 1, 1>{});
        std::mdspan C(cd, std::extents<int, 1, 1>{});
        calls.hermitian_matrix_product(A, std::linalg::upper_triangle, B, C);
        EXPECT_EQ(cd[0], 6.0);
    }
    { // 57: hermitian_matrix_rank_k_update(alpha, A, C, t)
        SCOPED_TRACE("57: hermitian_matrix_rank_k_update");
        double ad[]{2.0};
        double cd[]{0.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        std::mdspan C(cd, std::extents<int, 1, 1>{});
        calls.hermitian_matrix_rank_k_update(3.0, A, C, std::linalg::upper_triangle);
        EXPECT_EQ(cd[0], 12.0);
    }
    { // 58: hermitian_matrix_rank_k_update(alpha, A, E, C, t)
        SCOPED_TRACE("58: hermitian_matrix_rank_k_update");
        double ad[]{2.0};
        double cd[]{0.0};
        double ed[]{5.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        std::mdspan C(cd, std::extents<int, 1, 1>{});
        std::mdspan E(ed, std::extents<int, 1, 1>{});
        calls.hermitian_matrix_rank_k_update(3.0, A, E, C, std::linalg::upper_triangle);
        EXPECT_EQ(cd[0], 17.0);
    }
    { // 59: triangular_matrix_matrix_left_solve(A, t, d, b, x, divide)
        SCOPED_TRACE("59: triangular_matrix_matrix_left_solve");
        double ad[]{2.0};
        double cd[]{0.0};
        double xd[]{2.0};
        double rhs[]{6.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        std::mdspan x(xd, std::extents<int, 1>{});
        std::mdspan b(rhs, std::extents<int, 1, 1>{});
        std::mdspan X(cd, std::extents<int, 1, 1>{});
        auto divide = [](double numerator, double denominator) {
            EXPECT_EQ(denominator, 2.0);
            return numerator / denominator;
        };
        calls.triangular_matrix_matrix_left_solve(A, std::linalg::upper_triangle, std::linalg::explicit_diagonal, b, X, divide);
        EXPECT_EQ(cd[0], 3.0);
    }
    { // 60: triangular_matrix_matrix_left_solve(A, t, d, b, x)
        SCOPED_TRACE("60: triangular_matrix_matrix_left_solve");
        double ad[]{2.0};
        double cd[]{0.0};
        double xd[]{2.0};
        double rhs[]{6.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        std::mdspan x(xd, std::extents<int, 1>{});
        std::mdspan b(rhs, std::extents<int, 1, 1>{});
        std::mdspan X(cd, std::extents<int, 1, 1>{});
        calls.triangular_matrix_matrix_left_solve(A, std::linalg::upper_triangle, std::linalg::explicit_diagonal, b, X);
        EXPECT_EQ(cd[0], 3.0);
    }
    { // 61: triangular_matrix_matrix_left_solve(A, t, d, b, divide)
        SCOPED_TRACE("61: triangular_matrix_matrix_left_solve");
        double ad[]{2.0};
        double rhs[]{6.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        std::mdspan b(rhs, std::extents<int, 1, 1>{});
        auto divide = [](double numerator, double denominator) {
            EXPECT_EQ(denominator, 2.0);
            return numerator / denominator;
        };
        calls.triangular_matrix_matrix_left_solve(A, std::linalg::upper_triangle, std::linalg::explicit_diagonal, b, divide);
        EXPECT_EQ(rhs[0], 3.0);
    }
    { // 62: triangular_matrix_matrix_left_solve(A, t, d, b)
        SCOPED_TRACE("62: triangular_matrix_matrix_left_solve");
        double ad[]{2.0};
        double rhs[]{6.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        std::mdspan b(rhs, std::extents<int, 1, 1>{});
        calls.triangular_matrix_matrix_left_solve(A, std::linalg::upper_triangle, std::linalg::explicit_diagonal, b);
        EXPECT_EQ(rhs[0], 3.0);
    }
    { // 63: triangular_matrix_matrix_right_solve(A, t, d, b, x, divide)
        SCOPED_TRACE("63: triangular_matrix_matrix_right_solve");
        double ad[]{2.0};
        double cd[]{0.0};
        double xd[]{2.0};
        double rhs[]{6.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        std::mdspan x(xd, std::extents<int, 1>{});
        std::mdspan b(rhs, std::extents<int, 1, 1>{});
        std::mdspan X(cd, std::extents<int, 1, 1>{});
        auto divide = [](double numerator, double denominator) {
            EXPECT_EQ(denominator, 2.0);
            return numerator / denominator;
        };
        calls.triangular_matrix_matrix_right_solve(A, std::linalg::upper_triangle, std::linalg::explicit_diagonal, b, X, divide);
        EXPECT_EQ(cd[0], 3.0);
    }
    { // 64: triangular_matrix_matrix_right_solve(A, t, d, b, x)
        SCOPED_TRACE("64: triangular_matrix_matrix_right_solve");
        double ad[]{2.0};
        double cd[]{0.0};
        double xd[]{2.0};
        double rhs[]{6.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        std::mdspan x(xd, std::extents<int, 1>{});
        std::mdspan b(rhs, std::extents<int, 1, 1>{});
        std::mdspan X(cd, std::extents<int, 1, 1>{});
        calls.triangular_matrix_matrix_right_solve(A, std::linalg::upper_triangle, std::linalg::explicit_diagonal, b, X);
        EXPECT_EQ(cd[0], 3.0);
    }
    { // 65: triangular_matrix_matrix_right_solve(A, t, d, b, divide)
        SCOPED_TRACE("65: triangular_matrix_matrix_right_solve");
        double ad[]{2.0};
        double rhs[]{6.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        std::mdspan b(rhs, std::extents<int, 1, 1>{});
        auto divide = [](double numerator, double denominator) {
            EXPECT_EQ(denominator, 2.0);
            return numerator / denominator;
        };
        calls.triangular_matrix_matrix_right_solve(A, std::linalg::upper_triangle, std::linalg::explicit_diagonal, b, divide);
        EXPECT_EQ(rhs[0], 3.0);
    }
    { // 66: triangular_matrix_matrix_right_solve(A, t, d, b)
        SCOPED_TRACE("66: triangular_matrix_matrix_right_solve");
        double ad[]{2.0};
        double rhs[]{6.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        std::mdspan b(rhs, std::extents<int, 1, 1>{});
        calls.triangular_matrix_matrix_right_solve(A, std::linalg::upper_triangle, std::linalg::explicit_diagonal, b);
        EXPECT_EQ(rhs[0], 3.0);
    }
    { // 67: symmetric_matrix_product(A, t, B, E, C)
        SCOPED_TRACE("67: symmetric_matrix_product");
        double ad[]{2.0};
        double bd[]{3.0};
        double cd[]{0.0};
        double ed[]{5.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        std::mdspan B(bd, std::extents<int, 1, 1>{});
        std::mdspan C(cd, std::extents<int, 1, 1>{});
        std::mdspan E(ed, std::extents<int, 1, 1>{});
        calls.symmetric_matrix_product(A, std::linalg::upper_triangle, B, E, C);
        EXPECT_EQ(cd[0], 11.0);
    }
    { // 68: symmetric_matrix_product(A, B, t, C)
        SCOPED_TRACE("68: symmetric_matrix_product");
        double ad[]{2.0};
        double bd[]{3.0};
        double cd[]{0.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        std::mdspan B(bd, std::extents<int, 1, 1>{});
        std::mdspan C(cd, std::extents<int, 1, 1>{});
        calls.symmetric_matrix_product(A, B, std::linalg::upper_triangle, C);
        EXPECT_EQ(cd[0], 6.0);
    }
    { // 69: symmetric_matrix_product(A, B, t, E, C)
        SCOPED_TRACE("69: symmetric_matrix_product");
        double ad[]{2.0};
        double bd[]{3.0};
        double cd[]{0.0};
        double ed[]{5.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        std::mdspan B(bd, std::extents<int, 1, 1>{});
        std::mdspan C(cd, std::extents<int, 1, 1>{});
        std::mdspan E(ed, std::extents<int, 1, 1>{});
        calls.symmetric_matrix_product(A, B, std::linalg::upper_triangle, E, C);
        EXPECT_EQ(cd[0], 11.0);
    }
    { // 70: hermitian_matrix_product(A, t, B, E, C)
        SCOPED_TRACE("70: hermitian_matrix_product");
        double ad[]{2.0};
        double bd[]{3.0};
        double cd[]{0.0};
        double ed[]{5.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        std::mdspan B(bd, std::extents<int, 1, 1>{});
        std::mdspan C(cd, std::extents<int, 1, 1>{});
        std::mdspan E(ed, std::extents<int, 1, 1>{});
        calls.hermitian_matrix_product(A, std::linalg::upper_triangle, B, E, C);
        EXPECT_EQ(cd[0], 11.0);
    }
    { // 71: hermitian_matrix_product(A, B, t, C)
        SCOPED_TRACE("71: hermitian_matrix_product");
        double ad[]{2.0};
        double bd[]{3.0};
        double cd[]{0.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        std::mdspan B(bd, std::extents<int, 1, 1>{});
        std::mdspan C(cd, std::extents<int, 1, 1>{});
        calls.hermitian_matrix_product(A, B, std::linalg::upper_triangle, C);
        EXPECT_EQ(cd[0], 6.0);
    }
    { // 72: hermitian_matrix_product(A, B, t, E, C)
        SCOPED_TRACE("72: hermitian_matrix_product");
        double ad[]{2.0};
        double bd[]{3.0};
        double cd[]{0.0};
        double ed[]{5.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        std::mdspan B(bd, std::extents<int, 1, 1>{});
        std::mdspan C(cd, std::extents<int, 1, 1>{});
        std::mdspan E(ed, std::extents<int, 1, 1>{});
        calls.hermitian_matrix_product(A, B, std::linalg::upper_triangle, E, C);
        EXPECT_EQ(cd[0], 11.0);
    }
    { // 73: triangular_matrix_product(A, t, d, B, C)
        SCOPED_TRACE("73: triangular_matrix_product");
        double ad[]{2.0};
        double bd[]{3.0};
        double cd[]{0.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        std::mdspan B(bd, std::extents<int, 1, 1>{});
        std::mdspan C(cd, std::extents<int, 1, 1>{});
        calls.triangular_matrix_product(A, std::linalg::upper_triangle, std::linalg::explicit_diagonal, B, C);
        EXPECT_EQ(cd[0], 6.0);
    }
    { // 74: triangular_matrix_product(A, t, d, B, E, C)
        SCOPED_TRACE("74: triangular_matrix_product");
        double ad[]{2.0};
        double bd[]{3.0};
        double cd[]{0.0};
        double ed[]{5.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        std::mdspan B(bd, std::extents<int, 1, 1>{});
        std::mdspan C(cd, std::extents<int, 1, 1>{});
        std::mdspan E(ed, std::extents<int, 1, 1>{});
        calls.triangular_matrix_product(A, std::linalg::upper_triangle, std::linalg::explicit_diagonal, B, E, C);
        EXPECT_EQ(cd[0], 11.0);
    }
    { // 75: triangular_matrix_product(A, B, t, d, C)
        SCOPED_TRACE("75: triangular_matrix_product");
        double ad[]{2.0};
        double bd[]{3.0};
        double cd[]{0.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        std::mdspan B(bd, std::extents<int, 1, 1>{});
        std::mdspan C(cd, std::extents<int, 1, 1>{});
        calls.triangular_matrix_product(A, B, std::linalg::upper_triangle, std::linalg::explicit_diagonal, C);
        EXPECT_EQ(cd[0], 6.0);
    }
    { // 76: triangular_matrix_product(A, B, t, d, E, C)
        SCOPED_TRACE("76: triangular_matrix_product");
        double ad[]{2.0};
        double bd[]{3.0};
        double cd[]{0.0};
        double ed[]{5.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        std::mdspan B(bd, std::extents<int, 1, 1>{});
        std::mdspan C(cd, std::extents<int, 1, 1>{});
        std::mdspan E(ed, std::extents<int, 1, 1>{});
        calls.triangular_matrix_product(A, B, std::linalg::upper_triangle, std::linalg::explicit_diagonal, E, C);
        EXPECT_EQ(cd[0], 11.0);
    }
    { // 77: hermitian_matrix_rank_2k_update(A, B, C, t)
        SCOPED_TRACE("77: hermitian_matrix_rank_2k_update");
        double ad[]{2.0};
        double bd[]{3.0};
        double cd[]{0.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        std::mdspan B(bd, std::extents<int, 1, 1>{});
        std::mdspan C(cd, std::extents<int, 1, 1>{});
        calls.hermitian_matrix_rank_2k_update(A, B, C, std::linalg::upper_triangle);
        EXPECT_EQ(cd[0], 12.0);
    }
    { // 78: hermitian_matrix_rank_2k_update(A, B, E, C, t)
        SCOPED_TRACE("78: hermitian_matrix_rank_2k_update");
        double ad[]{2.0};
        double bd[]{3.0};
        double cd[]{0.0};
        double ed[]{5.0};
        std::mdspan A(ad, std::extents<int, 1, 1>{});
        std::mdspan B(bd, std::extents<int, 1, 1>{});
        std::mdspan C(cd, std::extents<int, 1, 1>{});
        std::mdspan E(ed, std::extents<int, 1, 1>{});
        calls.hermitian_matrix_rank_2k_update(A, B, E, C, std::linalg::upper_triangle);
        EXPECT_EQ(cd[0], 17.0);
    }
}

} // namespace linalg_conformance
