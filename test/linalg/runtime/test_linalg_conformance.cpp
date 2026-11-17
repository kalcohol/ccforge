#include "../proxy_cases.hpp"
#include <gtest/gtest.h>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>

namespace {
namespace la = std::linalg;
namespace p = unique_mapping_probe;

// Explicit real construction makes the old real/value conditional ill-formed.
// Separate assignment of a real result remains valid.
struct box {
    int re{}, im{};
    box() = default;
    explicit box(int r) : re(r) {}
    box(int r, int i) : re(r), im(i) {}
    box& operator=(int r) { re = r; im = 0; return *this; }
    box& operator+=(box b) { re += b.re; im += b.im; return *this; }
    friend box operator+(box a, box b) { return {a.re + b.re, a.im + b.im}; }
    friend box operator+(int a, box b) { return box(a) + b; }
    friend box operator*(box a, box b) { return {a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re}; }
    friend box operator*(int a, box b) { return box(a) * b; }
    friend box operator*(double a, box b) { return {int(a * b.re), int(a * b.im)}; }
    friend box operator*(std::complex<double> a, box b) {
        return {int(a.real() * b.re - a.imag() * b.im), int(a.real() * b.im + a.imag() * b.re)};
    }
    friend box conj(box a) { return {a.re, -a.im}; }
    friend int real(box a) { return a.re; }
    friend bool operator==(box, box) = default;
};
struct read_proxy {
    const box* value;
    operator box() const { return *value; }
    friend box conj(read_proxy) { return box(9999); }
};
box operator*(int a, read_proxy b) { return a * box(b); }
struct read_accessor {
    using offset_policy = read_accessor;
    using element_type = const box;
    using reference = read_proxy;
    using data_handle_type = const box*;
    reference access(data_handle_type p, std::size_t i) const { return {p + i}; }
    data_handle_type offset(data_handle_type p, std::size_t i) const { return p + i; }
};
} // namespace

TEST(LinalgConformance, MixedRhsProxySolves) {
    linalg_conformance::mixed_rhs_proxy_solves(linalg_conformance::sequential_calls{});
}

TEST(LinalgConformance, AssignmentOnlyOutputs) {
    linalg_conformance::assignment_only_outputs(linalg_conformance::sequential_calls{});
}

TEST(LinalgConformance, SolveWritesThroughNoncopyableProxies) {
    double ad[]{2, 1, 0, 2}, rhs[]{4, 4}, output[2]{};
    p::matrix a(ad);
    p::vector b(rhs);
    p::proxy_vector x(output);
    la::triangular_matrix_vector_solve(a, la::upper_triangle, la::explicit_diagonal, b, x);
    EXPECT_EQ(output[0], 1.0); EXPECT_EQ(output[1], 2.0);
    p::proxy_vector inout(rhs);
    la::triangular_matrix_vector_solve(a, la::upper_triangle, la::explicit_diagonal, inout);
    EXPECT_EQ(rhs[0], 1.0); EXPECT_EQ(rhs[1], 2.0);
    double left[]{5, 8, 6, 8}, right[]{2, 5, 6, 11};
    p::proxy_matrix l(left), r(right);
    la::triangular_matrix_matrix_left_solve(a, la::upper_triangle, la::explicit_diagonal, l);
    la::triangular_matrix_matrix_right_solve(a, la::upper_triangle, la::explicit_diagonal, r);
    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(left[i], double(i + 1)); EXPECT_EQ(right[i], double(i + 1));
    }
}

TEST(LinalgConformance, ConjugatesElementValuesAndAssignsRealDiagonals) {
    box xd[]{box(2, 1)}, yd[]{box(3, 2)}, ed[]{box(5, 9)}, out[1];
    using V = std::extents<int, 1>;
    using M = std::extents<int, 1, 1>;
    std::mdspan x(xd, V{}), y(yd, V{});
    std::mdspan a(xd, M{}), b(yd, M{}), e(ed, M{}), c(out, M{});
    std::mdspan<const box, V, std::layout_right, read_accessor> proxy(yd);
    std::mdspan<const box, M, std::layout_right, read_accessor> factor(xd);
    la::matrix_rank_1_update_c(x, proxy, c); EXPECT_EQ(out[0], box(8, -1));
    la::matrix_rank_1_update_c(x, proxy, e, c); EXPECT_EQ(out[0], box(13, 8));
    la::hermitian_matrix_rank_1_update(2, proxy, c, la::upper_triangle); EXPECT_EQ(out[0], box(26));
    la::hermitian_matrix_rank_1_update(2, proxy, e, c, la::upper_triangle); EXPECT_EQ(out[0], box(31));
    la::hermitian_matrix_rank_2_update(x, proxy, c, la::upper_triangle); EXPECT_EQ(out[0], box(16));
    la::hermitian_matrix_rank_2_update(x, proxy, e, c, la::upper_triangle); EXPECT_EQ(out[0], box(21));
    la::hermitian_matrix_rank_k_update(2, factor, c, la::upper_triangle); EXPECT_EQ(out[0], box(10));
    la::hermitian_matrix_rank_k_update(2, factor, e, c, la::upper_triangle); EXPECT_EQ(out[0], box(15));
    la::hermitian_matrix_rank_2k_update(factor, b, c, la::upper_triangle); EXPECT_EQ(out[0], box(16));
    la::hermitian_matrix_rank_2k_update(factor, b, e, c, la::upper_triangle); EXPECT_EQ(out[0], box(21));
    (void)a;
}

TEST(LinalgConformance, ComplexRotationDeducesRealFromCoefficientsOnly) {
    box xd[]{box(2, 1)}, yd[]{box(3, 2)};
    std::mdspan x(xd, std::extents<int, 1>{}), y(yd, std::extents<int, 1>{});
    la::apply_givens_rotation(x, y, 0.0, std::complex<double>(0, 1));
    EXPECT_EQ(xd[0], box(-2, 3)); EXPECT_EQ(yd[0], box(-1, 2));
}

TEST(LinalgConformance, HermitianFactorIsMultipliedBeforeOutputConversion) {
    double ad[]{1.0 + std::ldexp(1.0, -24)}, bd[]{3.0};
    float out[1]{};
    std::mdspan a(ad, std::extents<int, 1, 1>{});
    std::mdspan b(bd, std::extents<int, 1, 1>{});
    std::mdspan c(out, std::extents<int, 1, 1>{});
    la::hermitian_matrix_product(a, la::upper_triangle, b, c);
    // Forge expectation: this sequential kernel multiplies in double before the store.
    // The draft permits other floating-point evaluation/rounding strategies.
    EXPECT_EQ(out[0], static_cast<float>(ad[0] * bd[0]));
    EXPECT_NE(out[0], static_cast<float>(ad[0]) * static_cast<float>(bd[0]));
}

namespace {
struct hermitian_factor {
    int re{}, im{};
    friend int real(hermitian_factor a) { return a.re; }
    friend hermitian_factor conj(hermitian_factor a) { return {a.re, -a.im}; }
};
struct right_operand {
    int value{};
    friend box operator*(int a, right_operand b) { return box(a * b.value); }
    friend box operator*(hermitian_factor a, right_operand b) {
        return {a.re * b.value, a.im * b.value};
    }
};
static_assert(!std::is_convertible_v<hermitian_factor, box>);
}

TEST(LinalgConformance, HermitianMixedProductNeedsNoFactorToOutputConversion) {
    hermitian_factor ad[]{{1, 99}, {2, 1}, {999, 999}, {3, 99}};
    right_operand bd[]{{2}, {3}};
    box output[2];
    std::mdspan a(ad, std::extents<int, 2, 2>{});
    std::mdspan b(bd, std::extents<int, 2, 1>{});
    std::mdspan c(output, std::extents<int, 2, 1>{});
    la::hermitian_matrix_product(a, la::upper_triangle, b, c);
    EXPECT_EQ(output[0], box(8, 3)); EXPECT_EQ(output[1], box(13, -2));
}

TEST(LinalgConformance, RankOneInputPackedTriangleMayDiffer) {
    double input[]{2, 7, 3}, xd[]{1, 2}, out[4]{};
    p::lower_matrix e(input);
    p::vector x(xd);
    p::matrix c(out);
    la::symmetric_matrix_rank_1_update(2.0, x, e, c, la::upper_triangle);
    EXPECT_EQ(out[0], 4.0); EXPECT_EQ(out[1], 11.0); EXPECT_EQ(out[3], 11.0);
    la::hermitian_matrix_rank_1_update(2.0, x, e, c, la::upper_triangle);
    EXPECT_EQ(out[0], 4.0); EXPECT_EQ(out[1], 11.0); EXPECT_EQ(out[3], 11.0);
}

namespace {
// A division algebra makes reversed products observable. There is deliberately
// no operator/ or operator/=; custom divide is the only division operation.
struct quaternion {
    double r{}, i{}, j{}, k{};
    quaternion() = default;
    explicit quaternion(double real) : r(real) {}
    quaternion(double real, double a, double b, double c) : r(real), i(a), j(b), k(c) {}
    quaternion& operator+=(quaternion b) { r += b.r; i += b.i; j += b.j; k += b.k; return *this; }
    quaternion& operator-=(quaternion b) { r -= b.r; i -= b.i; j -= b.j; k -= b.k; return *this; }
    friend quaternion operator+(quaternion a, quaternion b) { return a += b; }
    friend quaternion operator*(quaternion a, quaternion b) {
        return {a.r*b.r - a.i*b.i - a.j*b.j - a.k*b.k,
                a.r*b.i + a.i*b.r + a.j*b.k - a.k*b.j,
                a.r*b.j - a.i*b.k + a.j*b.r + a.k*b.i,
                a.r*b.k + a.i*b.j - a.j*b.i + a.k*b.r};
    }
    friend bool operator==(quaternion, quaternion) = default;
};
quaternion inverse_unit(quaternion d) { return {d.r, -d.i, -d.j, -d.k}; }
}

TEST(LinalgConformance, CustomDivideAndProductsRespectNoncommutativeSides) {
    using Q = quaternion;
    const Q qi(0, 1, 0, 0), qj(0, 0, 1, 0), qk(0, 0, 0, 1);
    Q ad[]{qi, qj, Q{}, qk};
    Q expected[]{Q(1) + qj, qi + qk, Q(2) + qk, qj};
    Q left[4]{}, right[4]{}, output[4]{};
    using E = std::extents<int, 2, 2>;
    std::mdspan a(ad, E{}), solution(expected, E{}), b(left, E{}), r(right, E{}), x(output, E{});
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 2; ++j)
            for (int k = 0; k < 2; ++k) {
                if (i <= k) b[i, j] += a[i, k] * solution[k, j];
                if (k <= j) r[i, j] += solution[i, k] * a[k, j];
            }
    const std::array<Q, 4> saved_left{left[0], left[1], left[2], left[3]};
    const std::array<Q, 4> saved_right{right[0], right[1], right[2], right[3]};
    auto left_divide = [&](Q numerator, Q denominator) {
        EXPECT_TRUE(denominator == qi || denominator == qk);
        return inverse_unit(denominator) * numerator;
    };
    auto right_divide = [&](Q numerator, Q denominator) {
        EXPECT_TRUE(denominator == qi || denominator == qk);
        return numerator * inverse_unit(denominator);
    };
    auto check = [&](const Q* actual) {
        for (int i = 0; i < 4; ++i) EXPECT_EQ(actual[i], expected[i]);
    };
    la::triangular_matrix_matrix_left_solve(a, la::upper_triangle, la::explicit_diagonal, b, x, left_divide); check(output);
    la::triangular_matrix_matrix_left_solve(a, la::upper_triangle, la::explicit_diagonal, b, left_divide); check(left);
    la::triangular_matrix_matrix_right_solve(a, la::upper_triangle, la::explicit_diagonal, r, x, right_divide); check(output);
    la::triangular_matrix_matrix_right_solve(a, la::upper_triangle, la::explicit_diagonal, r, right_divide); check(right);
    la::triangular_matrix_product(a, la::upper_triangle, la::explicit_diagonal, solution, x);
    for (int i = 0; i < 4; ++i) EXPECT_EQ(output[i], saved_left[i]);
    la::triangular_matrix_product(solution, a, la::upper_triangle, la::explicit_diagonal, x);
    for (int i = 0; i < 4; ++i) EXPECT_EQ(output[i], saved_right[i]);
    Q rhs[]{saved_left[0], saved_left[2]}, vector_out[2]{};
    std::mdspan bv(rhs, std::extents<int, 2>{}), xv(vector_out, std::extents<int, 2>{});
    la::triangular_matrix_vector_solve(a, la::upper_triangle, la::explicit_diagonal, bv, xv, left_divide);
    EXPECT_EQ(vector_out[0], expected[0]); EXPECT_EQ(vector_out[1], expected[2]);
    la::triangular_matrix_vector_solve(a, la::upper_triangle, la::explicit_diagonal, bv, left_divide);
    EXPECT_EQ(rhs[0], expected[0]); EXPECT_EQ(rhs[1], expected[2]);
}

TEST(LinalgConformance, HermitianRank2kKeepsWideInnerExtentAndEmptySum) {
    using T = std::complex<double>;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    for (int inner : {0, 1, 256, 257}) {
        std::array<T, 514> av{}, bv{};
        std::mdspan a(av.data(), std::extents<int, 2, std::dynamic_extent>(inner));
        std::mdspan b(bv.data(), std::extents<int, 2, std::dynamic_extent>(inner));
        for (int i = 0; i < 2; ++i)
            for (int k = 0; k < inner; ++k) {
                a[i, k] = T(i + 1, 1); b[i, k] = T(i + 2, -1);
            }
        auto triangles = [&](auto t, bool upper) {
            T ev[4], cv[4];
            std::mdspan e(ev, std::extents<int, 2, 2>{});
            std::mdspan c(cv, std::extents<std::uint8_t, 2, 2>{});
            for (int i = 0; i < 2; ++i)
                for (int j = 0; j < 2; ++j)
                    e[i, j] = i == j ? T(5 + i, nan) : (upper == (i < j) ? T(7, 2) : T(nan, nan));
            for (bool update : {false, true}) {
                for (auto& v : cv) v = T(-99);
                if (update) la::hermitian_matrix_rank_2k_update(a, b, e, c, t);
                else la::hermitian_matrix_rank_2k_update(a, b, c, t);
                for (int i = 0; i < 2; ++i)
                    for (int j = 0; j < 2; ++j) {
                        if (i != j && upper != (i < j)) { EXPECT_EQ((c[i, j]), T(-99)); continue; }
                        T ref{};
                        for (int k = 0; k < inner; ++k)
                            ref += a[i, k] * std::conj(b[j, k]) + b[i, k] * std::conj(a[j, k]);
                        if (update) ref += i == j ? T(5 + i) : T(7, 2);
                        if (i == j) ref = T(ref.real());
                        EXPECT_EQ((c[i, j]), ref);
                    }
            }
        };
        triangles(la::upper_triangle, true); triangles(la::lower_triangle, false);
    }
}
