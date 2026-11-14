#include <gtest/gtest.h>
#include <linalg>
#include <mdspan>
#include <array>
#include <bit>
#include <complex>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <vector>

namespace {
using ext = std::dextents<std::size_t, 2>;

struct user_layout {
    template<class E> struct mapping : std::layout_right::mapping<E> {
        using base = std::layout_right::mapping<E>;
        using base::base;
        using layout_type = user_layout;
    };
};

template<class L>
auto mapping(std::size_t m, std::size_t n) {
    if constexpr (std::is_same_v<L, std::layout_stride>)
        return typename L::template mapping<ext>(ext(m, n),
            std::array<std::size_t, 2>{2 * n + 3, 2});
    else
        return typename L::template mapping<ext>(ext(m, n));
}

template<class T, class L>
struct matrix {
    typename L::template mapping<ext> map;
    std::vector<T> data;
    matrix(std::size_t m, std::size_t n)
        : map(mapping<L>(m, n)), data(map.required_span_size() + 1, T(-777)) {}
    auto view() { return std::mdspan<T, ext, L>(data.data(), map); }
};

template<class T, class L> void padding(const matrix<T, L>& a) {
    std::vector<bool> used(a.data.size(), false);
    for (std::size_t i = 0; i < a.map.extents().extent(0); ++i)
        for (std::size_t j = 0; j < a.map.extents().extent(1); ++j)
            used[a.map(i, j)] = true;
    for (std::size_t p = 0; p < used.size(); ++p)
        if (!used[p]) EXPECT_EQ(a.data[p], T(-777));
}

template<class T> T value(std::size_t i, std::size_t j) {
    const double r = (static_cast<int>((i * 7 + j * 3) % 13) - 6) / 10.0;
    if constexpr (std::is_same_v<T, std::complex<double>>)
        return {r, (static_cast<int>((i + j * 2) % 7) - 3) / 7.0};
    else return static_cast<T>(r);
}

template<class M> void fill(M a) {
    for (std::size_t i = 0; i < a.extent(0); ++i)
        for (std::size_t j = 0; j < a.extent(1); ++j)
            a[i, j] = value<typename M::value_type>(i, j);
}

template<class T> void exact(T actual, T expected) {
    if constexpr (std::is_same_v<T, double>)
        EXPECT_EQ(std::bit_cast<std::uint64_t>(actual), std::bit_cast<std::uint64_t>(expected));
    else if constexpr (std::is_same_v<T, float>)
        EXPECT_EQ(std::bit_cast<std::uint32_t>(actual), std::bit_cast<std::uint32_t>(expected));
    else if constexpr (std::is_same_v<T, std::complex<double>>) {
        exact(actual.real(), expected.real());
        exact(actual.imag(), expected.imag());
    }
    else EXPECT_EQ(actual, expected);
}

template<class A, class B, class C, class E>
void gemm(A a, B b, C c, E e) {
    using T = typename C::value_type;
    std::vector<T> ref(c.extent(0) * c.extent(1));
    for (std::size_t i = 0; i < c.extent(0); ++i)
        for (std::size_t j = 0; j < c.extent(1); ++j) {
            T sum{};
            for (std::size_t k = 0; k < a.extent(1); ++k) sum += a[i, k] * b[k, j];
            ref[i * c.extent(1) + j] = sum;
        }
    std::linalg::matrix_product(a, b, c);
    for (std::size_t i = 0; i < c.extent(0); ++i)
        for (std::size_t j = 0; j < c.extent(1); ++j)
            exact(T(c[i, j]), ref[i * c.extent(1) + j]);
    std::linalg::matrix_product(a, b, e, c);
    for (std::size_t i = 0; i < c.extent(0); ++i)
        for (std::size_t j = 0; j < c.extent(1); ++j)
            exact(T(c[i, j]), T(e[i, j] + ref[i * c.extent(1) + j]));
    // The updating input may alias the output exactly.
    std::linalg::matrix_product(a, b, c, c);
    for (std::size_t i = 0; i < c.extent(0); ++i)
        for (std::size_t j = 0; j < c.extent(1); ++j) {
            const auto sum = ref[i * c.extent(1) + j];
            exact(T(c[i, j]), T(T(e[i, j] + sum) + sum));
        }
}

// Copy-list-initializing an omitted array element would reject this constructor.
struct explicit_default_scalar {
    int value;
    explicit explicit_default_scalar() : value(0) {}
    explicit explicit_default_scalar(int v) : value(v) {}
    explicit_default_scalar& operator+=(explicit_default_scalar rhs) {
        value += rhs.value;
        return *this;
    }
    friend explicit_default_scalar operator*(explicit_default_scalar a, explicit_default_scalar b) {
        return explicit_default_scalar(a.value * b.value);
    }
    friend explicit_default_scalar operator+(explicit_default_scalar a, explicit_default_scalar b) {
        return explicit_default_scalar(a.value + b.value);
    }
    friend bool operator==(explicit_default_scalar, explicit_default_scalar) = default;
};

template<class L> void explicit_default_product() {
    using T = explicit_default_scalar;
    matrix<T, L> aa(9, 3), bb(3, 9), cc(9, 9), ee(9, 9);
    auto a = aa.view(); auto b = bb.view(); auto e = ee.view();
    for (std::size_t i = 0; i < 9; ++i)
        for (std::size_t k = 0; k < 3; ++k) {
            a[i, k] = T(int(i + k + 1));
            b[k, i] = T(int(i + 2 * k + 1));
        }
    for (std::size_t i = 0; i < 9; ++i)
        for (std::size_t j = 0; j < 9; ++j) e[i, j] = T(int(i + j));
    gemm(a, b, cc.view(), e);
    padding(aa); padding(bb); padding(cc); padding(ee);
}

TEST(LinalgProductLayouts, ExplicitDefaultScalarBothStripOrientations) {
    explicit_default_product<std::layout_right>();
    explicit_default_product<std::layout_left>();
}

template<bool Left, class Triangle, class Diagonal, class A, class C>
void trmm(A a, C c, Triangle triangle, Diagonal diagonal) {
    using T = typename C::value_type;
    constexpr bool upper = std::is_same_v<Triangle, std::linalg::upper_triangle_t>;
    constexpr bool unit = std::is_same_v<Diagonal, std::linalg::implicit_unit_diagonal_t>;
    fill(c);
    std::vector<T> ref(c.extent(0) * c.extent(1));
    for (std::size_t i = 0; i < c.extent(0); ++i)
        for (std::size_t j = 0; j < c.extent(1); ++j) {
            const auto d = Left ? i : j;
            const auto first = (Left ? upper : !upper) ? d : 0;
            const auto last = (Left ? upper : !upper) ? a.extent(0) : d + 1;
            T sum{};
            for (std::size_t k = first; k < last; ++k) {
                if constexpr (Left) {
                    if constexpr (unit) sum += (k == i ? c[k, j] : a[i, k] * c[k, j]);
                    else sum += a[i, k] * c[k, j];
                } else {
                    if constexpr (unit) sum += (k == j ? c[i, k] : c[i, k] * a[k, j]);
                    else sum += c[i, k] * a[k, j];
                }
            }
            ref[i * c.extent(1) + j] = sum;
        }
    if constexpr (Left) std::linalg::triangular_matrix_left_product(a, triangle, diagonal, c);
    else std::linalg::triangular_matrix_right_product(a, triangle, diagonal, c);
    for (std::size_t i = 0; i < c.extent(0); ++i)
        for (std::size_t j = 0; j < c.extent(1); ++j)
            EXPECT_LE(std::abs(T(c[i, j]) - ref[i * c.extent(1) + j]),
                16 * std::numeric_limits<decltype(std::abs(T{}))>::epsilon() * a.extent(0) *
                (1 + std::abs(ref[i * c.extent(1) + j])));
}

// Fixed rectangular outputs also check that dispatch through a plain transpose
// retains default accessors and reaches the contiguous update branches.
template<bool Left, class AT, class CT, class AL, class CL, bool Transpose>
void typed_trmm() {
    using namespace std::linalg;
    using AE = std::extents<std::size_t, 7, 7>;
    using CE = std::extents<std::size_t, Left ? 7 : 9, Left ? 9 : 7>;
    const typename AL::template mapping<AE> am(AE{});
    const typename CL::template mapping<CE> cm(CE{});
    std::vector<AT> ad(am.required_span_size() + 1, AT(-777));
    std::vector<CT> cd(cm.required_span_size() + 1, CT(-777));
    const std::mdspan<AT, AE, AL> original(ad.data(), am);
    const auto a = [&] {
        if constexpr (Transpose) return transposed(original);
        else return original;
    }();
    const std::mdspan<CT, CE, CL> c(cd.data(), cm);
    static_assert(std::is_same_v<typename decltype(a)::accessor_type,
                                 std::default_accessor<AT>>);
    static_assert(std::is_same_v<typename decltype(c)::accessor_type,
                                 std::default_accessor<CT>>);
    fill(a);
    trmm<Left>(a, c, upper_triangle, explicit_diagonal);
    trmm<Left>(a, c, lower_triangle, explicit_diagonal);
    for (std::size_t i = 0; i < 7; ++i)
        a[i, i] = AT(std::numeric_limits<double>::quiet_NaN());
    trmm<Left>(a, c, upper_triangle, implicit_unit_diagonal);
    trmm<Left>(a, c, lower_triangle, implicit_unit_diagonal);
    std::vector<bool> used(cd.size(), false);
    for (std::size_t i = 0; i < c.extent(0); ++i)
        for (std::size_t j = 0; j < c.extent(1); ++j) used[cm(i, j)] = true;
    for (std::size_t p = 0; p < cd.size(); ++p)
        if (!used[p]) EXPECT_EQ(cd[p], CT(-777));
}

template<class AT, class CT>
void mixed_trmm() {
    typed_trmm<true, AT, CT, std::layout_left, std::layout_left, false>();
    typed_trmm<false, AT, CT, std::layout_right, std::layout_right, false>();
}

TEST(LinalgProductLayouts, MixedElementTriangularProducts) {
    mixed_trmm<float, double>();
    mixed_trmm<double, float>();
    mixed_trmm<double, std::complex<double>>();
}

TEST(LinalgProductLayouts, PlainTransposeStaticRectangularProducts) {
    typed_trmm<true, double, double, std::layout_right, std::layout_left, true>();
    typed_trmm<false, double, double, std::layout_left, std::layout_right, true>();
    typed_trmm<true, double, double, std::layout_right_padded<8>,
               std::layout_left_padded<8>, true>();
    typed_trmm<false, double, double, std::layout_left_padded<8>,
               std::layout_right_padded<8>, true>();
}

template<class Triangle, class A, class B, class C>
void structured(A a, B b, C c, Triangle triangle) {
    using T = typename C::value_type;
    constexpr bool upper = std::is_same_v<Triangle, std::linalg::upper_triangle_t>;
    for (bool hermitian : {false, true}) {
        std::vector<T> ref(c.extent(0) * c.extent(1));
        for (std::size_t i = 0; i < c.extent(0); ++i)
            for (std::size_t j = 0; j < c.extent(1); ++j) {
                T sum{};
                for (std::size_t k = 0; k < a.extent(0); ++k) {
                    const bool stored = upper ? i <= k : i >= k;
                    T v = stored ? a[i, k] : a[k, i];
                    if constexpr (std::is_same_v<T, std::complex<double>>) {
                        if (hermitian) v = i == k ? T(v.real()) : (stored ? v : std::conj(v));
                    }
                    sum += v * b[k, j];
                }
                ref[i * c.extent(1) + j] = sum;
            }
        if (hermitian) std::linalg::hermitian_matrix_product(a, triangle, b, c);
        else std::linalg::symmetric_matrix_product(a, triangle, b, c);
        for (std::size_t i = 0; i < c.extent(0); ++i)
            for (std::size_t j = 0; j < c.extent(1); ++j)
                exact(T(c[i, j]), ref[i * c.extent(1) + j]);
    }
}

template<class T, class AL, class BL>
void pair() {
    using namespace std::linalg;
    for (std::size_t s : {0, 1, 2, 3, 7, 9, 33, 64}) {
        const std::size_t n = s + 2;
        matrix<T, AL> aa(s, s), cc(s, n);
        matrix<T, BL> bb(s, n), ee(s, n), rr(n, s);
        auto a = aa.view(); auto b = bb.view(); auto c = cc.view();
        fill(a); fill(b); fill(ee.view());
        gemm(a, b, c, ee.view());
        structured(a, b, c, upper_triangle);
        structured(a, b, c, lower_triangle);
        trmm<true>(a, c, upper_triangle, explicit_diagonal);
        trmm<true>(a, c, lower_triangle, explicit_diagonal);
        trmm<true>(a, b, upper_triangle, explicit_diagonal);
        trmm<true>(a, b, lower_triangle, explicit_diagonal);
        trmm<false>(a, rr.view(), upper_triangle, explicit_diagonal);
        trmm<false>(a, rr.view(), lower_triangle, explicit_diagonal);
        // Poison the implicit diagonal so any accidental use affects the result.
        for (std::size_t i = 0; i < s; ++i) a[i, i] = T(std::numeric_limits<double>::quiet_NaN());
        trmm<true>(a, c, upper_triangle, implicit_unit_diagonal);
        trmm<true>(a, c, lower_triangle, implicit_unit_diagonal);
        trmm<true>(a, b, upper_triangle, implicit_unit_diagonal);
        trmm<true>(a, b, lower_triangle, implicit_unit_diagonal);
        trmm<false>(a, rr.view(), upper_triangle, implicit_unit_diagonal);
        trmm<false>(a, rr.view(), lower_triangle, implicit_unit_diagonal);
        padding(aa); padding(bb); padding(cc); padding(rr); padding(ee);
    }
    matrix<T, AL> rectangular_a(3, 9), rectangular_c(3, 5);
    matrix<T, BL> rectangular_b(9, 5), rectangular_e(3, 5);
    fill(rectangular_a.view()); fill(rectangular_b.view()); fill(rectangular_e.view());
    gemm(rectangular_a.view(), rectangular_b.view(), rectangular_c.view(), rectangular_e.view());
    // Nonempty output with an empty reduction.
    matrix<T, AL> a(3, 0), c(3, 5);
    matrix<T, BL> b(0, 5), e(3, 5);
    fill(e.view()); gemm(a.view(), b.view(), c.view(), e.view());
}

template<class T, class AL> void row() {
    pair<T, AL, std::layout_right>();
    pair<T, AL, std::layout_left>();
    pair<T, AL, std::layout_stride>();
    pair<T, AL, std::layout_right_padded<8>>();
    pair<T, AL, std::layout_left_padded<8>>();
}

TEST(LinalgProductLayouts, MixedLayoutsAndAllTriangularTags) {
    row<double, std::layout_right>(); row<double, std::layout_left>();
    pair<double, user_layout, std::layout_left>();
    pair<double, std::layout_right, user_layout>();
    row<double, std::layout_stride>(); row<double, std::layout_right_padded<8>>();
    row<double, std::layout_left_padded<8>>();
    row<std::complex<double>, std::layout_right>();
    row<std::complex<double>, std::layout_left>();
    pair<float, std::layout_right, std::layout_right>();
    pair<float, std::layout_left, std::layout_left>();
    pair<float, std::layout_left, std::layout_right>();
    pair<float, std::layout_right, std::layout_left>();
}

template<class Triangle, class Order, class CL>
void packed_trmm(Triangle triangle) {
    using namespace std::linalg;
    using AL = layout_blas_packed<Triangle, Order>;
    constexpr bool upper = std::is_same_v<Triangle, upper_triangle_t>;
    for (std::size_t n : {1, 2, 3, 7, 33, 64}) {
        matrix<double, AL> aa(n, n);
        matrix<double, CL> right(n + 2, n), left(n, n + 2);
        auto a = aa.view();
        for (std::size_t i = 0; i < n; ++i)
            for (std::size_t j = 0; j < n; ++j)
                if (upper ? i <= j : i >= j) a[i, j] = value<double>(i, j);
        trmm<false>(a, right.view(), triangle, explicit_diagonal);
        trmm<true>(a, left.view(), triangle, explicit_diagonal);
        for (std::size_t i = 0; i < n; ++i)
            a[i, i] = std::numeric_limits<double>::quiet_NaN();
        trmm<false>(a, right.view(), triangle, implicit_unit_diagonal);
        trmm<true>(a, left.view(), triangle, implicit_unit_diagonal);
        padding(right); padding(left);
    }
}

TEST(LinalgProductLayouts, PackedTriangularInputs) {
    using namespace std::linalg;
    packed_trmm<upper_triangle_t, row_major_t, std::layout_right>(upper_triangle);
    packed_trmm<lower_triangle_t, row_major_t, std::layout_right>(lower_triangle);
    packed_trmm<upper_triangle_t, column_major_t, std::layout_right>(upper_triangle);
    packed_trmm<lower_triangle_t, column_major_t, std::layout_right>(lower_triangle);
    packed_trmm<upper_triangle_t, row_major_t, std::layout_left>(upper_triangle);
    packed_trmm<lower_triangle_t, row_major_t, std::layout_left>(lower_triangle);
    packed_trmm<upper_triangle_t, column_major_t, std::layout_left>(upper_triangle);
    packed_trmm<lower_triangle_t, column_major_t, std::layout_left>(lower_triangle);
}

TEST(LinalgProductLayouts, ComposedViews) {
    using namespace std::linalg;
    for (std::size_t n : {1, 2, 3, 7, 33, 64}) {
        matrix<std::complex<double>, std::layout_right_padded<8>> aa(n, n);
        matrix<std::complex<double>, std::layout_left> bb(n + 2, n), cc(n, n + 2), ee(n, n + 2);
        fill(aa.view()); fill(bb.view()); fill(ee.view());
        auto a = conjugated(scaled(2.0, transposed(aa.view())));
        auto b = conjugated(transposed(bb.view()));
        gemm(a, b, cc.view(), ee.view());
        structured(a, b, cc.view(), upper_triangle);
        structured(a, b, cc.view(), lower_triangle);
        trmm<true>(a, cc.view(), upper_triangle, explicit_diagonal);
        trmm<true>(a, cc.view(), lower_triangle, implicit_unit_diagonal);
        trmm<true>(a, cc.view(), lower_triangle, explicit_diagonal);
        trmm<true>(a, cc.view(), upper_triangle, implicit_unit_diagonal);
        trmm<false>(a, bb.view(), upper_triangle, implicit_unit_diagonal);
        trmm<false>(a, bb.view(), lower_triangle, explicit_diagonal);
        trmm<false>(a, bb.view(), upper_triangle, explicit_diagonal);
        trmm<false>(a, bb.view(), lower_triangle, implicit_unit_diagonal);
        trmm<false>(a, transposed(cc.view()), upper_triangle, explicit_diagonal);
        trmm<false>(a, transposed(cc.view()), lower_triangle, explicit_diagonal);
        trmm<false>(a, transposed(cc.view()), upper_triangle, implicit_unit_diagonal);
        trmm<false>(a, transposed(cc.view()), lower_triangle, implicit_unit_diagonal);
    }
}

// A non-pointer handle ensures kernels cannot accidentally bypass access().
struct counted_accessor {
    using element_type = double;
    using reference = double&;
    struct data_handle_type { double* data; };
    using offset_policy = counted_accessor;
    int* count;
    reference access(data_handle_type p, std::size_t i) const { ++*count; return p.data[i]; }
    data_handle_type offset(data_handle_type p, std::size_t i) const { return {p.data + i}; }
};

TEST(LinalgProductLayouts, StatefulAccessorAndNestedView) {
    using namespace std::linalg;
    std::array<double, 9> data{1,2,3,4,5,6,7,8,9};
    int calls = 0;
    std::mdspan<double, ext, std::layout_right, counted_accessor>
        a(counted_accessor::data_handle_type{data.data()}, ext(3,3), counted_accessor{&calls});
    matrix<double, std::layout_right> bb(3, 5), cc(3, 5), ee(3, 5);
    fill(bb.view()); fill(ee.view());
    calls = 0;
    matrix_product(a, bb.view(), cc.view());
    EXPECT_EQ(calls, 45);
    auto check_accesses = [&](auto triangle, auto diagonal) {
        constexpr bool unit = std::is_same_v<decltype(diagonal), implicit_unit_diagonal_t>;
        fill(cc.view());
        calls = 0;
        triangular_matrix_left_product(a, triangle, diagonal, cc.view());
        EXPECT_EQ(calls, unit ? 15 : 30);
        fill(cc.view());
        calls = 0;
        triangular_matrix_right_product(a, triangle, diagonal, transposed(cc.view()));
        EXPECT_EQ(calls, unit ? 15 : 30);
    };
    check_accesses(upper_triangle, explicit_diagonal);
    check_accesses(lower_triangle, explicit_diagonal);
    check_accesses(upper_triangle, implicit_unit_diagonal);
    check_accesses(lower_triangle, implicit_unit_diagonal);
    std::array<double, 15> output{};
    int writes = 0;
    std::mdspan<double, ext, std::layout_right, counted_accessor> counted_output(
        counted_accessor::data_handle_type{output.data()}, ext(3,5), counted_accessor{&writes});
    matrix_product(a, bb.view(), counted_output);
    EXPECT_EQ(writes, 15);

    gemm(scaled(2.0, transposed(a)), bb.view(), cc.view(), ee.view());
    structured(a, bb.view(), cc.view(), lower_triangle);
    trmm<true>(a, cc.view(), lower_triangle, explicit_diagonal);
    trmm<true>(a, cc.view(), upper_triangle, implicit_unit_diagonal);
    trmm<false>(a, transposed(cc.view()), upper_triangle, explicit_diagonal);
    trmm<false>(a, transposed(cc.view()), lower_triangle, implicit_unit_diagonal);
}
struct proxy_accessor {
    using element_type = double;
    struct reference {
        double* ptr;
        operator double() const { return *ptr; }
        void operator=(double value) const { *ptr = value; }
    };
    using data_handle_type = double*;
    using offset_policy = proxy_accessor;
    reference access(double* p, std::size_t i) const { return {p + i}; }
    double* offset(double* p, std::size_t i) const { return p + i; }
};

TEST(LinalgProductLayouts, WritableProxyOutput) {
    using namespace std::linalg;
    matrix<double, std::layout_right> aa(3, 3), bb(3, 3), ee(3, 3);
    fill(aa.view()); fill(bb.view()); fill(ee.view());
    std::array<double, 9> data{};
    std::mdspan<double, ext, std::layout_left, proxy_accessor>
        c(data.data(), ext(3, 3), proxy_accessor{});
    gemm(aa.view(), bb.view(), c, ee.view());
    trmm<true>(aa.view(), c, upper_triangle, explicit_diagonal);
    trmm<true>(aa.view(), c, lower_triangle, explicit_diagonal);
    trmm<true>(aa.view(), c, upper_triangle, implicit_unit_diagonal);
    trmm<true>(aa.view(), c, lower_triangle, implicit_unit_diagonal);
    trmm<false>(aa.view(), c, upper_triangle, explicit_diagonal);
    trmm<false>(aa.view(), c, lower_triangle, explicit_diagonal);
    trmm<false>(aa.view(), c, upper_triangle, implicit_unit_diagonal);
    trmm<false>(aa.view(), c, lower_triangle, implicit_unit_diagonal);
}
} // namespace
