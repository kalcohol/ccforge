#pragma once

#include "conformance_cases.hpp"
#include <array>
#include <limits>
#include <type_traits>

namespace linalg_conformance {

using complex = std::complex<double>;
template<class T> constexpr bool is_complex = std::is_same_v<T, complex>;
template<class T> T conjugate(T value) {
    if constexpr (is_complex<T>) return std::conj(value);
    else return value;
}
template<class T> T real_value(T value) {
    if constexpr (is_complex<T>) return T(value.real());
    else return value;
}
template<class T> T number(int real, int imag = 0) {
    if constexpr (is_complex<T>) return T(real, imag);
    else return T(real);
}
template<class T> T poison() { return T(std::numeric_limits<double>::quiet_NaN()); }

template<class T, class Layout, class Extents>
struct matrix_buffer {
    std::array<T, 64> data{};
    typename Layout::template mapping<Extents> mapping;
    explicit matrix_buffer(Extents e) : mapping(make_mapping(e)) {}
    static auto make_mapping(Extents e) {
        if constexpr (std::is_same_v<Layout, std::layout_stride>)
            return typename Layout::template mapping<Extents>(e, std::array<int, 2>{9, 2});
        else return typename Layout::template mapping<Extents>(e);
    }
    auto view() { return std::mdspan<T, Extents, Layout>(data.data(), mapping); }
};

template<class Triangle> constexpr bool upper = std::is_same_v<Triangle, std::linalg::upper_triangle_t>;
template<class Triangle> bool selected(int i, int j) { return upper<Triangle> ? i <= j : i >= j; }

// Values are small Gaussian integers; all specified products and solutions are
// exact in double. The oracles never invoke another linalg algorithm.
template<class A, class Triangle, class Diagonal>
void fill_triangle(A a, Triangle, Diagonal) {
    using T = typename A::value_type;
    constexpr bool unit = std::is_same_v<Diagonal, std::linalg::implicit_unit_diagonal_t>;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            if (selected<Triangle>(i, j))
                a[i, j] = i == j ? (unit ? poison<T>() : T(2 * (i + 1))) : number<T>(i + j + 1, i - j);
            else if constexpr (A::is_always_unique()) a[i, j] = poison<T>();
        }
}

template<class A, class Triangle, class Diagonal>
auto triangular_value(A a, Triangle, Diagonal, int i, int j) {
    using T = typename A::value_type;
    if (i == j && std::is_same_v<Diagonal, std::linalg::implicit_unit_diagonal_t>) return T(1);
    if (!selected<Triangle>(i, j)) return T{};
    return T(a[i, j]);
}

template<bool Right, class Calls, class A, class Triangle, class Diagonal>
void matrix_solves(const Calls& calls, A a, Triangle t, Diagonal d) {
    using T = typename A::value_type;
    constexpr int rows = Right ? 2 : 3;
    constexpr int cols = Right ? 3 : 2;
    using E = std::extents<int, rows, cols>;
    matrix_buffer<T, std::layout_left, E> rhs(E{});
    matrix_buffer<T, std::layout_right, E> result(E{});
    auto b = rhs.view(); auto x = result.view();
    auto solution = [](int i, int j) { return number<T>(i + j + 1, i - j); };
    std::array<T, rows * cols> original{};
    for (int i = 0; i < rows; ++i)
        for (int j = 0; j < cols; ++j) {
            T sum{};
            for (int k = 0; k < 3; ++k) {
                if constexpr (Right) sum += solution(i, k) * triangular_value(a, t, d, k, j);
                else sum += triangular_value(a, t, d, i, k) * solution(k, j);
            }
            original[i * cols + j] = sum;
        }
    auto reset = [&] {
        for (int i = 0; i < rows; ++i)
            for (int j = 0; j < cols; ++j) b[i, j] = original[i * cols + j];
    };
    auto check = [&](auto output) {
        for (int i = 0; i < rows; ++i)
            for (int j = 0; j < cols; ++j) EXPECT_EQ((output[i, j]), solution(i, j));
        // Independently multiply back, including every right-hand side entry.
        for (int i = 0; i < rows; ++i)
            for (int j = 0; j < cols; ++j) {
                T sum{};
                for (int k = 0; k < 3; ++k) {
                    if constexpr (Right) sum += output[i, k] * triangular_value(a, t, d, k, j);
                    else sum += triangular_value(a, t, d, i, k) * output[k, j];
                }
                EXPECT_EQ(sum, original[i * cols + j]);
            }
    };
    auto divide = [&](T numerator, T denominator) {
        // Denominator membership is specified; order and count are not.
        bool match = false;
        if constexpr (std::is_same_v<Diagonal, std::linalg::implicit_unit_diagonal_t>) match = denominator == T(1);
        else for (int i = 0; i < 3; ++i) match = match || denominator == T(a[i, i]);
        EXPECT_TRUE(match);
        return numerator / denominator;
    };
    reset();
    if constexpr (Right) calls.triangular_matrix_matrix_right_solve(a, t, d, b, x, divide);
    else calls.triangular_matrix_matrix_left_solve(a, t, d, b, x, divide);
    check(x);
    for (int i = 0; i < rows; ++i)
        for (int j = 0; j < cols; ++j) EXPECT_EQ((b[i, j]), original[i * cols + j]);
    if constexpr (Right) calls.triangular_matrix_matrix_right_solve(a, t, d, b, x);
    else calls.triangular_matrix_matrix_left_solve(a, t, d, b, x);
    check(x);
    reset();
    if constexpr (Right) calls.triangular_matrix_matrix_right_solve(a, t, d, b, divide);
    else calls.triangular_matrix_matrix_left_solve(a, t, d, b, divide);
    check(b);
    reset();
    if constexpr (Right) calls.triangular_matrix_matrix_right_solve(a, t, d, b);
    else calls.triangular_matrix_matrix_left_solve(a, t, d, b);
    check(b);
}

template<class Calls, class A, class Triangle, class Diagonal>
void vector_solves_and_product(const Calls& calls, A a, Triangle t, Diagonal d) {
    using T = typename A::value_type;
    std::array<T, 3> input{}, original{}, output{}, addend{};
    for (int i = 0; i < 3; ++i) input[i] = number<T>(i + 1, 1 - i);
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) original[i] += triangular_value(a, t, d, i, j) * input[j];
    auto data = original;
    std::mdspan b(data.data(), std::extents<int, 3>{});
    std::mdspan x(output.data(), std::extents<int, 3>{});
    auto check = [&](auto v) {
        for (int i = 0; i < 3; ++i) EXPECT_EQ(v[i], input[i]);
        for (int i = 0; i < 3; ++i) {
            T sum{};
            for (int j = 0; j < 3; ++j) sum += triangular_value(a, t, d, i, j) * v[j];
            EXPECT_EQ(sum, original[i]);
        }
    };
    auto divide = [&](T n, T denominator) {
        bool match = false;
        if constexpr (std::is_same_v<Diagonal, std::linalg::implicit_unit_diagonal_t>) match = denominator == T(1);
        else for (int i = 0; i < 3; ++i) match = match || denominator == T(a[i, i]);
        EXPECT_TRUE(match);
        return n / denominator;
    };
    calls.triangular_matrix_vector_solve(a, t, d, b, x, divide); check(x);
    EXPECT_EQ(data, original);
    calls.triangular_matrix_vector_solve(a, t, d, b, x); check(x);
    EXPECT_EQ(data, original);
    calls.triangular_matrix_vector_solve(a, t, d, b, divide); check(b);
    data = original;
    calls.triangular_matrix_vector_solve(a, t, d, b); check(b);
    std::mdspan v(input.data(), std::extents<int, 3>{});
    for (int i = 0; i < 3; ++i) addend[i] = number<T>(4 + i, 2);
    std::mdspan y(addend.data(), std::extents<int, 3>{});
    calls.triangular_matrix_vector_product(a, t, d, v, y, x);
    for (int i = 0; i < 3; ++i) EXPECT_EQ(x[i], addend[i] + original[i]);
    const auto saved = addend;
    calls.triangular_matrix_vector_product(a, t, d, v, y, y);
    for (int i = 0; i < 3; ++i) EXPECT_EQ(y[i], saved[i] + original[i]);
}

// Kind: 0 triangular, 1 symmetric, 2 Hermitian. A is always the structured
// operand here; its position in the public call changes for right products.
template<int Kind, bool Right, class Calls, class A, class Triangle, class Diagonal>
void products(const Calls& calls, A a, Triangle t, Diagonal d) {
    using T = typename A::value_type;
    constexpr int rows = Right ? 2 : 3;
    constexpr int cols = Right ? 3 : 2;
    using E = std::extents<int, rows, cols>;
    matrix_buffer<T, std::layout_left, E> input(E{}), base(E{});
    matrix_buffer<T, std::layout_right, E> result(E{});
    auto b = input.view(); auto e = base.view(); auto c = result.view();
    for (int i = 0; i < rows; ++i)
        for (int j = 0; j < cols; ++j) {
            b[i, j] = number<T>(i + j + 1, j);
            e[i, j] = number<T>(i + j + 3, i);
        }
    auto effective = [&](int i, int j) -> T {
        if constexpr (Kind == 0) return triangular_value(a, t, d, i, j);
        else {
            if constexpr (Kind == 2) if (i == j) return real_value(T(a[i, i]));
            if (selected<Triangle>(i, j)) return T(a[i, j]);
            if constexpr (Kind == 2) return conjugate(T(a[j, i]));
            else return T(a[j, i]);
        }
    };
    std::array<T, rows * cols> expected{};
    for (int i = 0; i < rows; ++i)
        for (int j = 0; j < cols; ++j)
            for (int k = 0; k < 3; ++k) {
                if constexpr (Right) expected[i * cols + j] += b[i, k] * effective(k, j);
                else expected[i * cols + j] += effective(i, k) * b[k, j];
            }
    auto overwrite = [&] {
        if constexpr (Kind == 0) {
            if constexpr (Right) calls.triangular_matrix_product(b, a, t, d, c);
            else calls.triangular_matrix_product(a, t, d, b, c);
        } else if constexpr (Kind == 1) {
            if constexpr (Right) calls.symmetric_matrix_product(b, a, t, c);
            else calls.symmetric_matrix_product(a, t, b, c);
        } else {
            if constexpr (Right) calls.hermitian_matrix_product(b, a, t, c);
            else calls.hermitian_matrix_product(a, t, b, c);
        }
    };
    auto update = [&](auto source) {
        if constexpr (Kind == 0) {
            if constexpr (Right) calls.triangular_matrix_product(b, a, t, d, source, c);
            else calls.triangular_matrix_product(a, t, d, b, source, c);
        } else if constexpr (Kind == 1) {
            if constexpr (Right) calls.symmetric_matrix_product(b, a, t, source, c);
            else calls.symmetric_matrix_product(a, t, b, source, c);
        } else {
            if constexpr (Right) calls.hermitian_matrix_product(b, a, t, source, c);
            else calls.hermitian_matrix_product(a, t, b, source, c);
        }
    };
    overwrite();
    for (int i = 0; i < rows; ++i)
        for (int j = 0; j < cols; ++j) EXPECT_EQ((c[i, j]), expected[i * cols + j]);
    for (bool alias : {false, true}) {
        for (int i = 0; i < rows; ++i)
            for (int j = 0; j < cols; ++j) c[i, j] = e[i, j];
        if (alias) update(c); else update(e);
        for (int i = 0; i < rows; ++i)
            for (int j = 0; j < cols; ++j)
                EXPECT_EQ((c[i, j]), (e[i, j] + expected[i * cols + j]));
    }
}

template<class Calls, class A, class Triangle, class Diagonal>
void triangle_cases(const Calls& calls, A a, Triangle t, Diagonal d) {
    fill_triangle(a, t, d);
    vector_solves_and_product(calls, a, t, d);
    matrix_solves<false>(calls, a, t, d);
    matrix_solves<true>(calls, a, t, d);
    products<0, false>(calls, a, t, d);
    products<0, true>(calls, a, t, d);
    // Symmetric/Hermitian products have explicit diagonals. Poison only the
    // Hermitian diagonal's imaginary part, which must not participate.
    fill_triangle(a, t, std::linalg::explicit_diagonal);
    products<1, false>(calls, a, t, std::linalg::explicit_diagonal);
    products<1, true>(calls, a, t, std::linalg::explicit_diagonal);
    if constexpr (is_complex<typename A::value_type>)
        for (int i = 0; i < 3; ++i) a[i, i] = complex(2 * (i + 1), std::numeric_limits<double>::quiet_NaN());
    products<2, false>(calls, a, t, std::linalg::explicit_diagonal);
    products<2, true>(calls, a, t, std::linalg::explicit_diagonal);
}

template<class T, class Layout, class Calls, class Triangle>
void layout_cases(const Calls& calls, Triangle t) {
    matrix_buffer<T, Layout, std::extents<int, 3, 3>> fixed(std::extents<int, 3, 3>{});
    triangle_cases(calls, fixed.view(), t, std::linalg::explicit_diagonal);
    triangle_cases(calls, fixed.view(), t, std::linalg::implicit_unit_diagonal);
    matrix_buffer<T, Layout, std::dextents<int, 2>> dynamic(std::dextents<int, 2>{3, 3});
    triangle_cases(calls, dynamic.view(), t, std::linalg::explicit_diagonal);
    triangle_cases(calls, dynamic.view(), t, std::linalg::implicit_unit_diagonal);
}

template<class Calls, class T, class Triangle, class OutputLayout>
void rank2k(const Calls& calls, Triangle t) {
    using namespace std::linalg;
    matrix_buffer<T, std::layout_left, std::extents<int, 3, 2>> aa(std::extents<int, 3, 2>{});
    matrix_buffer<T, std::layout_right, std::extents<int, 3, 2>> bb(std::extents<int, 3, 2>{});
    matrix_buffer<T, OutputLayout, std::dextents<int, 2>> cc(std::dextents<int, 2>{3, 3});
    matrix_buffer<T, OutputLayout, std::dextents<int, 2>> ee(std::dextents<int, 2>{3, 3});
    auto a = aa.view(); auto b = bb.view(); auto c = cc.view(); auto e = ee.view();
    for (int i = 0; i < 3; ++i)
        for (int k = 0; k < 2; ++k) {
            a[i, k] = number<T>(i + k + 1, i - k);
            b[i, k] = number<T>(2 * i + k, k + 1);
        }
    std::array<T, 9> ref{}, initial{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            for (int k = 0; k < 2; ++k)
                ref[i * 3 + j] += a[i, k] * conjugate(T(b[j, k])) + b[i, k] * conjugate(T(a[j, k]));
            initial[i * 3 + j] = number<T>(i + j + 5, i - j);
            if (i == j && is_complex<T>) initial[i * 3 + j] = poison<T>();
        }
    auto reset = [&](bool alias) {
        cc.data.fill(T(-99)); ee.data.fill(poison<T>());
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) if (selected<Triangle>(i, j)) {
                T v = initial[i * 3 + j];
                if constexpr (is_complex<T>) if (i == j) v = T(i + j + 5, std::numeric_limits<double>::quiet_NaN());
                e[i, j] = v;
                if (alias) c[i, j] = v;
            }
    };
    auto check = [&](bool update) {
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) {
                if (selected<Triangle>(i, j)) {
                    T expected = ref[i * 3 + j];
                    if (update) expected += i == j ? T(i + j + 5) : initial[i * 3 + j];
                    if (i == j) expected = real_value(expected);
                    EXPECT_EQ((c[i, j]), expected);
                } else if constexpr (decltype(c)::is_always_unique()) EXPECT_EQ((c[i, j]), T(-99));
            }
    };
    reset(false); calls.hermitian_matrix_rank_2k_update(a, b, c, t); check(false);
    reset(false); calls.hermitian_matrix_rank_2k_update(a, b, e, c, t); check(true);
    reset(true); calls.hermitian_matrix_rank_2k_update(a, b, c, c, t); check(true);
    // A and B through lazy views use the same independent oracle on their values.
    reset(false);
    calls.hermitian_matrix_rank_2k_update(conjugated(scaled(T(1), a)), conjugated(b), c, t);
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) if (selected<Triangle>(i, j))
            EXPECT_EQ((c[i, j]), conjugate(ref[i * 3 + j]));
}

template<class Calls, class Triangle>
void triangle_suite(const Calls& calls, Triangle t) {
    using namespace std::linalg;
    using packed_row = layout_blas_packed<Triangle, row_major_t>;
    using packed_col = layout_blas_packed<Triangle, column_major_t>;
    layout_cases<double, std::layout_right>(calls, t);
    layout_cases<double, std::layout_left>(calls, t);
    layout_cases<complex, std::layout_right>(calls, t);
    layout_cases<complex, std::layout_left>(calls, t);
    layout_cases<complex, std::layout_stride>(calls, t);
    layout_cases<complex, packed_row>(calls, t);
    layout_cases<complex, packed_col>(calls, t);
    rank2k<Calls, double, Triangle, std::layout_right>(calls, t);
    rank2k<Calls, complex, Triangle, std::layout_left>(calls, t);
    rank2k<Calls, complex, Triangle, packed_row>(calls, t);
    rank2k<Calls, complex, Triangle, packed_col>(calls, t);
    matrix_buffer<complex, std::layout_right, std::extents<int, 3, 3>> storage(std::extents<int, 3, 3>{});
    auto a = transposed(storage.view());
    triangle_cases(calls, a, t, implicit_unit_diagonal);
    fill_triangle(a, t, explicit_diagonal);
    // Views are inputs only: their element type is const, with value accessors.
    vector_solves_and_product(calls, scaled(2.0, a), t, explicit_diagonal);
    matrix_solves<false>(calls, conjugated(a), t, explicit_diagonal);
    products<0, true>(calls, conjugated(a), t, explicit_diagonal);
}

template<class Calls> void extended_cases(const Calls& calls) {
    triangle_suite(calls, std::linalg::upper_triangle);
    triangle_suite(calls, std::linalg::lower_triangle);
}

} // namespace linalg_conformance
