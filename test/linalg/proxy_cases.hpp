#pragma once

#include "conformance_cases.hpp"
#include "unique_mapping_probe.hpp"
#include <limits>
#include <type_traits>

namespace linalg_conformance {

// No proxy-level conj: only conversion to the complex element value is valid.
struct complex_read_proxy {
    const std::complex<double>* value;
    operator std::complex<double>() const { return *value; }
    friend std::complex<double> operator*(complex_read_proxy a, std::complex<double> b) {
        return *a.value * b;
    }
};
struct complex_read_accessor {
    using offset_policy = complex_read_accessor;
    using element_type = const std::complex<double>;
    using reference = complex_read_proxy;
    using data_handle_type = const std::complex<double>*;
    reference access(data_handle_type p, std::size_t i) const { return {p + i}; }
    data_handle_type offset(data_handle_type p, std::size_t i) const { return p + i; }
};

template<class Calls> void mixed_rhs_proxy_solves(const Calls& calls) {
    namespace p = unique_mapping_probe;
    namespace la = std::linalg;
    using complex = std::complex<double>;
    static_assert(std::is_convertible_v<p::write_proxy, double>);
    static_assert(!std::is_convertible_v<p::write_proxy, complex>);
    const double nan = std::numeric_limits<double>::quiet_NaN();
    complex ad[]{complex(2), complex(1), complex(nan, nan), complex(2)};
    std::mdspan a(ad, p::extents_2x2{});
    double vd[]{4, 4}, ld[]{5, 8, 6, 8}, rd[]{2, 5, 6, 11};
    p::proxy_vector b(vd);
    p::proxy_matrix left(ld), right(rd);
    complex vout[2]{}, mout[4]{};
    std::mdspan x(vout, p::extents_2{});
    std::mdspan c(mout, p::extents_2x2{});
    auto divide = [](complex numerator, complex denominator) { return numerator / denominator; };
    // Exercise the default and explicit divide bodies for all three families.
    for (bool custom : {false, true}) {
        if (custom) calls.triangular_matrix_vector_solve(a, la::upper_triangle, la::explicit_diagonal, b, x, divide);
        else calls.triangular_matrix_vector_solve(a, la::upper_triangle, la::explicit_diagonal, b, x);
        EXPECT_EQ(vout[0], complex(1)); EXPECT_EQ(vout[1], complex(2));
        if (custom) calls.triangular_matrix_matrix_left_solve(a, la::upper_triangle, la::explicit_diagonal, left, c, divide);
        else calls.triangular_matrix_matrix_left_solve(a, la::upper_triangle, la::explicit_diagonal, left, c);
        for (int i = 0; i < 4; ++i) EXPECT_EQ(mout[i], complex(i + 1));
        if (custom) calls.triangular_matrix_matrix_right_solve(a, la::upper_triangle, la::explicit_diagonal, right, c, divide);
        else calls.triangular_matrix_matrix_right_solve(a, la::upper_triangle, la::explicit_diagonal, right, c);
        for (int i = 0; i < 4; ++i) EXPECT_EQ(mout[i], complex(i + 1));
    }
    EXPECT_EQ(vd[0], 4.0); EXPECT_EQ(vd[1], 4.0);
    const double expected_left[]{5, 8, 6, 8}, expected_right[]{2, 5, 6, 11};
    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(ld[i], expected_left[i]); EXPECT_EQ(rd[i], expected_right[i]);
    }
}

// swap_elements also accepts ordinary references to move-only values.
struct movable_element {
    int value{};
    movable_element() = default;
    explicit movable_element(int v) : value(v) {}
    movable_element(const movable_element&) = delete;
    movable_element(movable_element&&) = default;
    movable_element& operator=(const movable_element&) = delete;
    movable_element& operator=(movable_element&&) = default;
};

template<class Calls> void assignment_only_outputs(const Calls& calls) {
    namespace p = unique_mapping_probe;
    namespace la = std::linalg;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    double xd[]{1, 2}, output[]{-99, -99};
    p::vector x(xd);
    p::proxy_vector y(output);
    auto check = [&](auto triangle, double* ad) {
        p::matrix a(ad);
        calls.symmetric_matrix_vector_product(a, triangle, x, y);
        EXPECT_EQ(output[0], 8.0); EXPECT_EQ(output[1], 13.0);
        output[0] = output[1] = -99;
        calls.hermitian_matrix_vector_product(a, triangle, x, y);
        EXPECT_EQ(output[0], 8.0); EXPECT_EQ(output[1], 13.0);
    };
    double upper[]{2, 3, nan, 5}, lower[]{2, nan, 3, 5};
    check(la::upper_triangle, upper); check(la::lower_triangle, lower);
    double swap_data[]{7, 9};
    p::proxy_vector z(swap_data);
    calls.swap_elements(y, z);
    EXPECT_EQ(output[0], 7.0); EXPECT_EQ(output[1], 9.0);
    EXPECT_EQ(swap_data[0], 8.0); EXPECT_EQ(swap_data[1], 13.0);
    double first[]{1, 2, 3, 4}, second[]{5, 6, 7, 8};
    p::proxy_matrix a(first), b(second);
    calls.swap_elements(a, b);
    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(first[i], double(i + 5)); EXPECT_EQ(second[i], double(i + 1));
    }
    movable_element move_first[]{movable_element(1), movable_element(2), movable_element(3), movable_element(4)};
    movable_element move_second[]{movable_element(5), movable_element(6), movable_element(7), movable_element(8)};
    std::mdspan mv1(move_first, p::extents_2{}), mv2(move_second, p::extents_2{});
    calls.swap_elements(mv1, mv2);
    EXPECT_EQ(move_first[0].value, 5); EXPECT_EQ(move_first[1].value, 6);
    EXPECT_EQ(move_second[0].value, 1); EXPECT_EQ(move_second[1].value, 2);
    std::mdspan mm1(move_first, p::extents_2x2{}), mm2(move_second, p::extents_2x2{});
    calls.swap_elements(mm1, mm2);
    EXPECT_EQ(move_first[0].value, 1); EXPECT_EQ(move_first[1].value, 2);
    EXPECT_EQ(move_second[0].value, 5); EXPECT_EQ(move_second[1].value, 6);
    EXPECT_EQ(move_first[2].value, 7); EXPECT_EQ(move_first[3].value, 8);
    EXPECT_EQ(move_second[2].value, 3); EXPECT_EQ(move_second[3].value, 4);
}

template<class Calls> void complex_proxy_dotc(const Calls& calls) {
    using complex = std::complex<double>;
    using E = std::extents<int, 2>;
    complex xd[]{complex(1, 2), complex(3, -1)}, yd[]{complex(2, -1), complex(1, 3)};
    std::mdspan<const complex, E, std::layout_right, complex_read_accessor> x(xd);
    std::mdspan y(yd, E{});
    // conj(1+2i)*(2-i) + conj(3-i)*(1+3i) == 5i.
    EXPECT_EQ(calls.dotc(x, y), complex(0, 5));
    EXPECT_EQ(calls.dotc(x, y, complex(7, -2)), complex(7, 3));
    // Double conjugation must restore the original accessor semantics.
    EXPECT_EQ(calls.dotc(std::linalg::conjugated(x), y), complex(10, 11));
}

} // namespace linalg_conformance
