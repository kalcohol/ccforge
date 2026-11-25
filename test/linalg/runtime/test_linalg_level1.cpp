#include <gtest/gtest.h>
#include <linalg>
#include <mdspan>
#include <array>
#include <cmath>
#include <complex>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>

namespace linalg_norm_test {

struct real {
    double value{};

    constexpr real() = default;
    constexpr real(double v) : value(v) {}
    constexpr operator double() const { return value; }
};

} // namespace linalg_norm_test

// Use an explicit specialization for the program-defined real type.
template<>
class std::complex<linalg_norm_test::real> {
public:
    using value_type = linalg_norm_test::real;

    constexpr complex(const value_type& re = {}, const value_type& im = {})
        : components_{re, im} {}

    constexpr complex(const complex&) = default;

    template<class X>
    constexpr explicit complex(const std::complex<X>& z)
        : complex(static_cast<value_type>(z.real()), static_cast<value_type>(z.imag())) {}

    constexpr value_type real() const { return components_[0]; }
    constexpr void real(value_type value) { components_[0] = value; }
    constexpr value_type imag() const { return components_[1]; }
    constexpr void imag(value_type value) { components_[1] = value; }

    constexpr complex& operator=(const complex&) = default;

    constexpr complex& operator=(const value_type& rhs) {
        return *this = complex{rhs};
    }

    constexpr complex& operator+=(const value_type& rhs) {
        real(real() + rhs);
        return *this;
    }

    constexpr complex& operator-=(const value_type& rhs) {
        real(real() - rhs);
        return *this;
    }

    constexpr complex& operator*=(const value_type& rhs) {
        const value_type factor = rhs;
        real(real() * factor);
        imag(imag() * factor);
        return *this;
    }

    constexpr complex& operator/=(const value_type& rhs) {
        const value_type divisor = rhs;
        real(real() / divisor);
        imag(imag() / divisor);
        return *this;
    }

    template<class X>
    constexpr complex& operator=(const std::complex<X>& rhs) {
        real(static_cast<value_type>(rhs.real()));
        imag(static_cast<value_type>(rhs.imag()));
        return *this;
    }

    template<class X>
    constexpr complex& operator+=(const std::complex<X>& rhs) {
        using arithmetic = arithmetic_type<X>;
        return *this = as_complex<arithmetic>(*this) + as_complex<arithmetic>(rhs);
    }

    template<class X>
    constexpr complex& operator-=(const std::complex<X>& rhs) {
        using arithmetic = arithmetic_type<X>;
        return *this = as_complex<arithmetic>(*this) - as_complex<arithmetic>(rhs);
    }

    template<class X>
    constexpr complex& operator*=(const std::complex<X>& rhs) {
        using arithmetic = arithmetic_type<X>;
        return *this = as_complex<arithmetic>(*this) * as_complex<arithmetic>(rhs);
    }

    template<class X>
    constexpr complex& operator/=(const std::complex<X>& rhs) {
        using arithmetic = arithmetic_type<X>;
        return *this = as_complex<arithmetic>(*this) / as_complex<arithmetic>(rhs);
    }

private:
    template<class X>
    using arithmetic_type =
        std::common_type_t<double, std::conditional_t<std::same_as<X, value_type>, double, X>>;

    template<class Arithmetic, class X>
    static constexpr std::complex<Arithmetic> as_complex(const std::complex<X>& z) {
        return {static_cast<Arithmetic>(z.real()), static_cast<Arithmetic>(z.imag())};
    }

    value_type components_[2];
};

namespace linalg_norm_test {

using complex = std::complex<real>;

static_assert(std::is_convertible_v<const complex&, complex>);
static_assert(std::is_constructible_v<complex, const std::complex<long double>&>);
static_assert(!std::is_convertible_v<const std::complex<long double>&, complex>);

struct rvalue_magnitude {
    real value;

    constexpr operator real() const { return value; }

    friend constexpr double operator*(rvalue_magnitude&& lhs, rvalue_magnitude&& rhs) {
        return lhs.value * rhs.value;
    }
};

rvalue_magnitude abs(const complex& z) { return {std::hypot(z.real().value, z.imag().value)}; }

template<class Magnitude>
concept lvalue_magnitude_product = requires(Magnitude& value) { value * value; };

static_assert(std::same_as<decltype(abs(complex{}) * abs(complex{})), double>);
static_assert(!lvalue_magnitude_product<decltype(abs(complex{}))>);

template<class T>
concept standard_givens_setup = requires(T value) {
    std::linalg::setup_givens_rotation(value, value);
};

static_assert(!standard_givens_setup<complex>);

} // namespace linalg_norm_test

namespace {

struct counting_norm_accessor {
    using element_type = double;
    using reference = double&;
    using data_handle_type = double*;
    using offset_policy = counting_norm_accessor;
    int* copies;

    explicit counting_norm_accessor(int& count) : copies(&count) {}
    counting_norm_accessor(const counting_norm_accessor& other) : copies(other.copies) {
        ++*copies;
    }
    counting_norm_accessor& operator=(const counting_norm_accessor&) = default;
    reference access(data_handle_type p, std::size_t i) const noexcept { return p[i]; }
    data_handle_type offset(data_handle_type p, std::size_t i) const noexcept { return p + i; }
};

TEST(LinalgLevel1Reductions, VectorTwoNormPreservesAccessorCopyCount) {
    using span = std::mdspan<double, std::extents<int, 2>,
                             std::layout_right, counting_norm_accessor>;
    int copies = 0;
    double data[]{3.0, 4.0};
    span x(data, span::mapping_type{}, counting_norm_accessor{copies});

    // Originally explicit-init copied once at the public by-value boundary;
    // default-init copied once more when delegating to explicit-init.
    copies = 0;
    EXPECT_DOUBLE_EQ(std::linalg::vector_two_norm(x, 0.0), 5.0);
    EXPECT_EQ(copies, 1);
    copies = 0;
    EXPECT_DOUBLE_EQ(std::linalg::vector_two_norm(x), 5.0);
    EXPECT_EQ(copies, 2);
}

TEST(LinalgLevel1Reductions, NormsAndRotationsPreserveAccessorCopyCount) {
    using vector = std::mdspan<double, std::extents<int, 2>,
                               std::layout_right, counting_norm_accessor>;
    using matrix = std::mdspan<double, std::extents<int, 1, 2>,
                               std::layout_right, counting_norm_accessor>;
    int copies = 0;
    double data[]{3.0, 4.0};
    vector v(data, vector::mapping_type{}, counting_norm_accessor{copies});
    matrix a(data, matrix::mapping_type{}, counting_norm_accessor{copies});
    copies = 0;
    EXPECT_DOUBLE_EQ(std::linalg::matrix_frob_norm(a, 0.0), 5.0);
    EXPECT_EQ(copies, 1);
    copies = 0;
    EXPECT_DOUBLE_EQ(std::linalg::matrix_frob_norm(a), 5.0);
    EXPECT_EQ(copies, 2);
    double other[]{0.0, 0.0};
    vector y(other, vector::mapping_type{}, counting_norm_accessor{copies});
    copies = 0;
    std::linalg::apply_givens_rotation(v, y, 1.0, 0.0);
    EXPECT_EQ(copies, 2);
}

template<class T>
struct rotation_reference {
    T* pointer;
    operator T() const noexcept { return *pointer; }
    rotation_reference& operator=(T value) noexcept {
        *pointer = value;
        return *this;
    }
};

template<class T>
struct rotation_accessor {
    using element_type = T;
    using reference = rotation_reference<T>;
    using data_handle_type = T*;
    using offset_policy = rotation_accessor;
    reference access(T* pointer, std::size_t index) const noexcept { return {pointer + index}; }
    T* offset(T* pointer, std::size_t index) const noexcept { return pointer + index; }
};

TEST(LinalgLevel1Givens, ProxyAccessorsSnapshotBothInputs) {
    using extents = std::extents<int, 1>;
    using proxy_span = std::mdspan<double, extents, std::layout_right, rotation_accessor<double>>;
    double x_data = 3.0;
    double y_data = 4.0;
    proxy_span x(&x_data, extents{}, rotation_accessor<double>{});
    proxy_span y(&y_data, extents{}, rotation_accessor<double>{});
    std::linalg::apply_givens_rotation(x, y, 0.0, 1.0);
    EXPECT_DOUBLE_EQ(x_data, 4.0);
    EXPECT_DOUBLE_EQ(y_data, -3.0);
    std::linalg::apply_givens_rotation(x, y, 0.0, -1.0);
    EXPECT_DOUBLE_EQ(x_data, 3.0);
    EXPECT_DOUBLE_EQ(y_data, 4.0);

    using complex = std::complex<double>;
    using complex_span = std::mdspan<complex, extents, std::layout_right, rotation_accessor<complex>>;
    complex cx_data{3.0, 2.0};
    complex cy_data{4.0, 1.0};
    const auto expected_x = complex{0.0, 1.0} * cy_data;
    const auto expected_y = complex{0.0, 1.0} * cx_data;
    complex_span cx(&cx_data, extents{}, rotation_accessor<complex>{});
    complex_span cy(&cy_data, extents{}, rotation_accessor<complex>{});
    std::linalg::apply_givens_rotation(cx, cy, 0.0, complex{0.0, 1.0});
    EXPECT_EQ(cx_data, expected_x);
    EXPECT_EQ(cy_data, expected_y);
    std::linalg::apply_givens_rotation(cx, cy, 0.0, complex{0.0, -1.0});
    EXPECT_EQ(cx_data, (complex{3.0, 2.0}));
    EXPECT_EQ(cy_data, (complex{4.0, 1.0}));
}

struct adl_dot_number {
    std::complex<double> value{};
    int* products{};
    int* conjugations{};
    int* conversions{};

    operator std::complex<double>() const {
        ++*conversions;
        return value;
    }
};

std::complex<double> operator*(const adl_dot_number& left,
                               const adl_dot_number& right) {
    ++*left.products;
    return left.value * right.value;
}

adl_dot_number conj(const adl_dot_number& value) {
    ++*value.conjugations;
    return {std::conj(value.value), value.products,
            value.conjugations, value.conversions};
}

template<class T, class Product = T>
struct dot_proxy {
    const T* value;
    int* products;
    int* conversions;

    operator T() const {
        ++*conversions;
        return *value;
    }

    friend Product operator*(dot_proxy left, dot_proxy right) {
        ++*left.products;
        return static_cast<Product>(*left.value * *right.value);
    }
};

template<class T, class Product = T>
struct dot_proxy_accessor {
    using offset_policy = dot_proxy_accessor;
    using element_type = const T;
    using reference = dot_proxy<T, Product>;
    using data_handle_type = const T*;

    int* products{};
    int* conversions{};

    constexpr reference access(data_handle_type pointer,
                               std::size_t index) const noexcept {
        return {pointer + index, products, conversions};
    }

    constexpr data_handle_type offset(data_handle_type pointer,
                                      std::size_t index) const noexcept {
        return pointer + index;
    }
};

struct conjugating_dot_value {
    double value{};

    constexpr conjugating_dot_value() = default;
    constexpr conjugating_dot_value(double input) : value(input) {}
    constexpr operator double() const noexcept { return value; }
    constexpr conjugating_dot_value& operator+=(double right) noexcept {
        value += right;
        return *this;
    }
    constexpr conjugating_dot_value& operator*=(double right) noexcept {
        value *= right;
        return *this;
    }
};

long double conj(conjugating_dot_value value) {
    return static_cast<long double>(value.value);
}

using conjugating_dot_proxy =
    dot_proxy<conjugating_dot_value, long double>;

long double conj(conjugating_dot_proxy value) {
    return conj(*value.value);
}

long double operator*(long double left, conjugating_dot_proxy right) {
    ++*right.products;
    return left * right.value->value;
}

} // namespace

TEST(LinalgLevel1CopyScaleSwapAdd, DenseVectors) {
    double x_data[] = {1.0, 2.0, 3.0};
    double y_data[] = {4.0, 5.0, 6.0};
    double z_data[] = {0.0, 0.0, 0.0};
    std::mdspan x(x_data, std::extents<int, 3>{});
    std::mdspan y(y_data, std::extents<int, 3>{});
    std::mdspan z(z_data, std::extents<int, 3>{});

    std::linalg::copy(x, z);
    EXPECT_DOUBLE_EQ(z[0], 1.0);
    EXPECT_DOUBLE_EQ(z[1], 2.0);
    EXPECT_DOUBLE_EQ(z[2], 3.0);

    std::linalg::scale(2.0, z);
    EXPECT_DOUBLE_EQ(z[0], 2.0);
    EXPECT_DOUBLE_EQ(z[1], 4.0);
    EXPECT_DOUBLE_EQ(z[2], 6.0);

    std::linalg::swap_elements(x, y);
    EXPECT_DOUBLE_EQ(x[0], 4.0);
    EXPECT_DOUBLE_EQ(y[0], 1.0);

    std::linalg::add(x, y, z);
    EXPECT_DOUBLE_EQ(z[0], 5.0);
    EXPECT_DOUBLE_EQ(z[1], 7.0);
    EXPECT_DOUBLE_EQ(z[2], 9.0);
}

TEST(LinalgLevel1CopyScaleSwapAdd, DenseMatrices) {
    double a_data[] = {1.0, 2.0, 3.0, 4.0};
    double b_data[] = {5.0, 6.0, 7.0, 8.0};
    double c_data[] = {0.0, 0.0, 0.0, 0.0};
    std::mdspan a(a_data, std::extents<int, 2, 2>{});
    std::mdspan b(b_data, std::extents<int, 2, 2>{});
    std::mdspan c(c_data, std::extents<int, 2, 2>{});

    std::linalg::copy(a, c);
    EXPECT_DOUBLE_EQ((c[1, 1]), 4.0);

    std::linalg::scale(3.0, c);
    EXPECT_DOUBLE_EQ((c[0, 1]), 6.0);
    EXPECT_DOUBLE_EQ((c[1, 1]), 12.0);

    std::linalg::swap_elements(a, b);
    EXPECT_DOUBLE_EQ((a[0, 0]), 5.0);
    EXPECT_DOUBLE_EQ((b[1, 1]), 4.0);

    std::linalg::add(a, b, c);
    EXPECT_DOUBLE_EQ((c[0, 0]), 6.0);
    EXPECT_DOUBLE_EQ((c[1, 1]), 12.0);
}

TEST(LinalgLevel1CompatibleExtents, AcceptsDifferentMdspanExtentsTypes) {
    std::array<double, 3> x_data{1.0, 2.0, 3.0};
    std::array<double, 3> y_data{4.0, 5.0, 6.0};
    std::array<double, 3> z_data{};

    std::mdspan<double, std::extents<std::size_t, 3>> x(x_data.data());
    std::mdspan<double, std::dextents<int, 1>> y(y_data.data(), 3);
    std::mdspan<double, std::extents<long, std::dynamic_extent>> z(
        z_data.data(), 3);

    EXPECT_DOUBLE_EQ(std::linalg::dot(x, y, 0.0), 32.0);
    EXPECT_DOUBLE_EQ(std::linalg::dotc(x, y, 0.0), 32.0);
    EXPECT_DOUBLE_EQ(std::linalg::dot(x, y), 32.0);
    EXPECT_DOUBLE_EQ(std::linalg::dotc(x, y), 32.0);

    std::linalg::add(x, y, z);
    EXPECT_EQ(z_data, (std::array<double, 3>{5.0, 7.0, 9.0}));

    std::linalg::swap_elements(x, y);
    EXPECT_EQ(x_data, (std::array<double, 3>{4.0, 5.0, 6.0}));
    EXPECT_EQ(y_data, (std::array<double, 3>{1.0, 2.0, 3.0}));

    std::linalg::apply_givens_rotation(x, y, 0.0, 1.0);
    EXPECT_EQ(x_data, (std::array<double, 3>{1.0, 2.0, 3.0}));
    EXPECT_EQ(y_data, (std::array<double, 3>{-4.0, -5.0, -6.0}));
}

TEST(LinalgLevel1Dot, LayoutStrideUsesMappedElements) {
    double x_data[] = {1.0, 100.0, 2.0, 100.0, 3.0};
    double y_data[] = {4.0, 100.0, 5.0, 100.0, 6.0};
    using extents_t = std::dextents<int, 1>;
    using mapping_t = std::layout_stride::mapping<extents_t>;
    mapping_t mapping(extents_t{3}, std::array<int, 1>{2});
    std::mdspan<double, extents_t, std::layout_stride> x(x_data, mapping);
    std::mdspan<double, extents_t, std::layout_stride> y(y_data, mapping);

    EXPECT_DOUBLE_EQ(std::linalg::dot(x, y), 32.0);
}

TEST(LinalgLevel1DotSIMD, DoubleEquivalence) {
    constexpr int N = 1024;
    static double x[N], y[N];
    for (int i = 0; i < N; ++i) { x[i] = static_cast<double>(i+1); y[i] = 1.0/(i+1); }
    std::mdspan xv(x, std::extents<int, N>{});
    std::mdspan yv(y, std::extents<int, N>{});
    double result = std::linalg::dot(xv, yv, 0.0);
    EXPECT_NEAR(result, static_cast<double>(N), 1e-10);
}

TEST(LinalgLevel1Dot, WiderInitAccumulatesProductsInInitType) {
    float x_data[] = {1.0e20f};
    float y_data[] = {1.0e20f};
    std::mdspan x(x_data, std::extents<int, 1>{});
    std::mdspan y(y_data, std::extents<int, 1>{});

    const double result = std::linalg::dot(x, y, 0.0);
    EXPECT_TRUE(std::isfinite(result));
    EXPECT_NEAR(result, 1.0e40, 1.0e34);
}

TEST(LinalgLevel1Dotc, RealAndComplexInputs) {
    double real_x_data[] = {1.0, 2.0, 3.0};
    double real_y_data[] = {4.0, 5.0, 6.0};
    std::mdspan real_x(real_x_data, std::extents<int, 3>{});
    std::mdspan real_y(real_y_data, std::extents<int, 3>{});
    EXPECT_DOUBLE_EQ(std::linalg::dotc(real_x, real_y), 32.0);

    using complex = std::complex<double>;
    complex complex_x_data[] = {{1.0, 1.0}, {2.0, -1.0}};
    complex complex_y_data[] = {{3.0, 2.0}, {4.0, -1.0}};
    std::mdspan complex_x(complex_x_data, std::extents<int, 2>{});
    std::mdspan complex_y(complex_y_data, std::extents<int, 2>{});

    EXPECT_EQ(std::linalg::dotc(complex_x, complex_y), complex(14.0, 1.0));
}

TEST(LinalgLevel1Dotc, WiderInitAccumulatesProductsInInitType) {
    float x_data[] = {1.0e20f};
    float y_data[] = {1.0e20f};
    std::mdspan x(x_data, std::extents<int, 1>{});
    std::mdspan y(y_data, std::extents<int, 1>{});

    const double result = std::linalg::dotc(x, y, 0.0);
    EXPECT_TRUE(std::isfinite(result));
    EXPECT_NEAR(result, 1.0e40, 1.0e34);
}

TEST(LinalgLevel1DotProducts, IntegralInitMultipliesBeforeConversion) {
    double x_data[] = {0.5, 0.5};
    double y_data[] = {4.0, 4.0};
    std::mdspan x(x_data, std::extents<int, 2>{});
    std::mdspan y(y_data, std::extents<int, 2>{});

    static_assert(std::is_same_v<decltype(std::linalg::dot(x, y, 0)), int>);
    static_assert(std::is_same_v<decltype(std::linalg::dotc(x, y, 0)), int>);
    for (int init : {0, 7, -7}) {
        SCOPED_TRACE(init);
        EXPECT_EQ(std::linalg::dot(x, y, init), init + 4);
        EXPECT_EQ(std::linalg::dotc(x, y, init), init + 4);
    }
}

TEST(LinalgLevel1DotProducts, NarrowInitDoesNotNarrowFactors) {
    double x_data[] = {1.0 + 0x1p-23};
    double y_data[] = {1.0 - 0x1p-23};
    std::mdspan x(x_data, std::extents<int, 1>{});
    std::mdspan y(y_data, std::extents<int, 1>{});

    EXPECT_EQ(std::linalg::dot(x, y, -1.0f), -0x1p-46f);
    EXPECT_EQ(std::linalg::dotc(x, y, -1.0f), -0x1p-46f);
}

TEST(LinalgLevel1DotProducts, WorkTypeIncludesFactorWiderThanInit) {
    if constexpr (std::numeric_limits<long double>::digits >
                  std::numeric_limits<double>::digits) {
        const long double delta =
            std::ldexp(1.0L, -std::numeric_limits<double>::digits);
        long double x_data[] = {1.0L + delta};
        float y_data[] = {1.0f};
        std::mdspan x(x_data, std::extents<int, 1>{});
        std::mdspan y(y_data, std::extents<int, 1>{});

        EXPECT_EQ(std::linalg::dot(x, y, -1.0), static_cast<double>(delta));
        EXPECT_EQ(std::linalg::dotc(x, y, -1.0), static_cast<double>(delta));
    }
}

TEST(LinalgLevel1DotProducts, WideComplexInitPreservesProductPrecision) {
    using complex_float = std::complex<float>;
    using complex_double = std::complex<double>;
    const float high = 1.0f + 0x1p-23f;
    const float low = 1.0f - 0x1p-23f;
    complex_float x_data[] = {{high, high}};
    complex_float y_data[] = {{low, low}};
    std::mdspan x(x_data, std::extents<int, 1>{});
    std::mdspan y(y_data, std::extents<int, 1>{});
    const complex_double init{0.5, -0.25};
    const double product_component = 2.0 - 0x1p-45;

    EXPECT_EQ(std::linalg::dot(x, y, init),
              init + complex_double(0.0, product_component));
    EXPECT_EQ(std::linalg::dotc(x, y, init),
              init + complex_double(product_component, 0.0));
}

TEST(LinalgLevel1DotProducts, NarrowComplexInitDoesNotNarrowFactors) {
    using complex_float = std::complex<float>;
    using complex_double = std::complex<double>;
    complex_double x_data[] = {{1.0 + 0x1p-23, 0.0}};
    complex_double y_data[] = {{1.0 - 0x1p-23, 0.0}};
    std::mdspan x(x_data, std::extents<int, 1>{});
    std::mdspan y(y_data, std::extents<int, 1>{});
    const complex_float init{-1.0f, 0.0f};
    const complex_float expected{-0x1p-46f, 0.0f};

    EXPECT_EQ(std::linalg::dot(x, y, init), expected);
    EXPECT_EQ(std::linalg::dotc(x, y, init), expected);
}

TEST(LinalgLevel1DotProducts, GenericFactorsUseTheirMultiplyAndAdlConj) {
    using complex = std::complex<double>;
    int products = 0;
    int conjugations = 0;
    int conversions = 0;
    adl_dot_number x_data[] = {
        {{1.0, 1.0}, &products, &conjugations, &conversions},
        {{2.0, -1.0}, &products, &conjugations, &conversions},
    };
    adl_dot_number y_data[] = {
        {{3.0, 2.0}, &products, &conjugations, &conversions},
        {{4.0, -1.0}, &products, &conjugations, &conversions},
    };
    std::mdspan x(x_data, std::extents<int, 2>{});
    std::mdspan y(y_data, std::extents<int, 2>{});

    EXPECT_EQ(std::linalg::dot(x, y, complex{}), complex(8.0, -1.0));
    EXPECT_EQ(products, 2);
    EXPECT_EQ(conjugations, 0);
    EXPECT_EQ(conversions, 0);

    products = 0;
    EXPECT_EQ(std::linalg::dot(x, y), complex(8.0, -1.0));
    EXPECT_EQ(products, 2);
    EXPECT_EQ(conjugations, 0);
    EXPECT_EQ(conversions, 0);

    products = 0;
    EXPECT_EQ(std::linalg::dotc(x, y, complex{}), complex(14.0, 1.0));
    EXPECT_EQ(products, 2);
    EXPECT_EQ(conjugations, 2);
    EXPECT_EQ(conversions, 0);

    products = 0;
    conjugations = 0;
    EXPECT_EQ(std::linalg::dotc(x, y), complex(14.0, 1.0));
    EXPECT_EQ(products, 2);
    EXPECT_EQ(conjugations, 2);
    EXPECT_EQ(conversions, 0);
}

TEST(LinalgLevel1DotProducts, ProxyReferencesKeepTheirMultiply) {
    double x_data[] = {0.5, 0.5};
    double y_data[] = {4.0, 4.0};
    int products = 0;
    int conversions = 0;
    using proxy_span = std::mdspan<const double, std::extents<int, 2>,
                                   std::layout_right, dot_proxy_accessor<double>>;
    const dot_proxy_accessor<double> accessor{&products, &conversions};
    proxy_span x(x_data, proxy_span::mapping_type{}, accessor);
    proxy_span y(y_data, proxy_span::mapping_type{}, accessor);

    EXPECT_EQ(std::linalg::dot(x, y, 0), 4);
    EXPECT_EQ(products, 2);
    EXPECT_EQ(conversions, 0);

    products = 0;
    EXPECT_EQ(std::linalg::dotc(x, y, 0), 4);
    EXPECT_EQ(products, 2);
    EXPECT_EQ(conversions, 0);

    products = 0;
    EXPECT_EQ(std::linalg::dot(x, y, 7.0), 11.0);
    EXPECT_EQ(products, 2);
    EXPECT_EQ(conversions, 0);

    products = 0;
    EXPECT_EQ(std::linalg::dotc(x, y, 7.0), 11.0);
    EXPECT_EQ(products, 2);
    EXPECT_EQ(conversions, 0);

    products = 0;
    EXPECT_EQ(std::linalg::dot(x, y), 4.0);
    EXPECT_EQ(products, 2);
    EXPECT_EQ(conversions, 0);

    products = 0;
    EXPECT_EQ(std::linalg::dotc(x, y), 4.0);
    EXPECT_EQ(products, 2);
    EXPECT_EQ(conversions, 0);
}

TEST(LinalgLevel1DotProducts, DefaultProxyResultsUseValueTypes) {
    float x_data[] = {1.0f, 1.0f};
    float y_data[] = {1.0f, 0x1p-24f};
    int products = 0;
    int conversions = 0;
    using accessor = dot_proxy_accessor<float, double>;
    using proxy_span = std::mdspan<const float, std::extents<int, 2>,
                                   std::layout_right, accessor>;
    proxy_span x(x_data, proxy_span::mapping_type{}, accessor{&products, &conversions});
    proxy_span y(y_data, proxy_span::mapping_type{}, accessor{&products, &conversions});

    static_assert(std::semiregular<accessor> && std::is_nothrow_swappable_v<accessor>);
    static_assert(std::common_reference_with<accessor::reference&&, const float&>);
    static_assert(std::is_same_v<proxy_span::value_type, float>);
    static_assert(std::is_same_v<decltype(x[0] * y[0]), double>);
    static_assert(std::is_same_v<decltype(std::linalg::dot(x, y)), float>);
    static_assert(std::is_same_v<decltype(std::linalg::dotc(x, y)), float>);
    static_assert(std::is_same_v<decltype(std::linalg::dot(x, y, 0.0f)), float>);
    static_assert(std::is_same_v<decltype(std::linalg::dotc(x, y, 0.0f)), float>);
    static_assert(std::is_same_v<decltype(std::linalg::dot(x, y, 7.0)), double>);
    static_assert(std::is_same_v<decltype(std::linalg::dotc(x, y, 7.0)), double>);

    for (bool conjugate : {false, true}) {
        SCOPED_TRACE(conjugate);
        products = conversions = 0;
        EXPECT_EQ(conjugate ? std::linalg::dotc(x, y) : std::linalg::dot(x, y), 1.0f);
        EXPECT_EQ(products, 2);
        EXPECT_EQ(conversions, 0);

        products = conversions = 0;
        EXPECT_EQ(conjugate ? std::linalg::dotc(x, y, 0.0f)
                            : std::linalg::dot(x, y, 0.0f), 1.0f);
        EXPECT_EQ(products, 2);
        EXPECT_EQ(conversions, 0);

        products = conversions = 0;
        EXPECT_EQ(conjugate ? std::linalg::dotc(x, y, 7.0)
                            : std::linalg::dot(x, y, 7.0), 8.0 + 0x1p-24);
        EXPECT_EQ(products, 0);
        EXPECT_EQ(conversions, 4);
    }
}

TEST(LinalgLevel1DotProducts, DefaultDotcUsesConjugatedValueType) {
    conjugating_dot_value x_data[] = {2.0, 3.0};
    conjugating_dot_value y_data[] = {4.0, 5.0};
    int products = 0;
    int conversions = 0;
    using accessor = dot_proxy_accessor<conjugating_dot_value, long double>;
    using proxy_span = std::mdspan<const conjugating_dot_value, std::extents<int, 2>,
                                   std::layout_right, accessor>;
    proxy_span x(x_data, proxy_span::mapping_type{}, accessor{&products, &conversions});
    proxy_span y(y_data, proxy_span::mapping_type{}, accessor{&products, &conversions});
    std::mdspan dense_x(x_data, std::extents<int, 2>{});
    std::mdspan dense_y(y_data, std::extents<int, 2>{});

    static_assert(std::semiregular<conjugating_dot_value>);
    static_assert(std::semiregular<double> && std::semiregular<long double>);
    static_assert(conjugating_dot_value{}.value == 0.0);
    static_assert(proxy_span::rank() == 1 && decltype(dense_x)::rank() == 1);
    static_assert(std::semiregular<accessor> && std::is_nothrow_swappable_v<accessor>);
    static_assert(std::is_nothrow_move_constructible_v<accessor> &&
                  std::is_nothrow_move_assignable_v<accessor>);
    static_assert(std::common_reference_with<accessor::reference&&,
                                             const conjugating_dot_value&>);
    static_assert(std::is_same_v<decltype(conj(std::declval<proxy_span::value_type>())),
                                 long double>);
    static_assert(std::is_same_v<decltype(x[0] * y[0]), long double>);
    static_assert(std::is_same_v<decltype(std::linalg::dot(x, y)), double>);
    static_assert(std::is_same_v<decltype(std::linalg::dotc(x, y)), long double>);
    static_assert(std::is_same_v<decltype(std::linalg::dot(dense_x, dense_y)), double>);
    static_assert(std::is_same_v<decltype(std::linalg::dotc(dense_x, dense_y)), long double>);
    static_assert(std::is_same_v<decltype(std::linalg::dot(x, y, 7.0L)), long double>);
    static_assert(std::is_same_v<decltype(std::linalg::dotc(x, y, 7.0)), double>);

    EXPECT_EQ(std::linalg::dot(x, y), 23.0);
    EXPECT_EQ(products, 2);
    EXPECT_EQ(conversions, 0);
    // dotc is dot(conjugated(v1), v2, init): the conjugated accessor converts
    // each v1 reference to its element type before conj-if-needed.
    products = conversions = 0;
    EXPECT_EQ(std::linalg::dotc(x, y), 23.0L);
    EXPECT_EQ(products, 2);
    EXPECT_EQ(conversions, 2);
    products = conversions = 0;
    EXPECT_EQ(std::linalg::dot(x, y, 7.0L), 30.0L);
    EXPECT_EQ(products, 2);
    EXPECT_EQ(conversions, 0);
    products = conversions = 0;
    EXPECT_EQ(std::linalg::dotc(x, y, 7.0), 30.0);
    EXPECT_EQ(products, 2);
    EXPECT_EQ(conversions, 2);
    EXPECT_EQ(std::linalg::dot(dense_x, dense_y), 23.0);
    EXPECT_EQ(std::linalg::dotc(dense_x, dense_y), 23.0L);
}

TEST(LinalgLevel1DotProducts, DefaultProxyEmptyInputUsesValueTypeZero) {
    float data[] = {1.0f};
    int products = 0;
    int conversions = 0;
    using accessor = dot_proxy_accessor<float, double>;
    using proxy_span = std::mdspan<const float, std::extents<int, 0>,
                                   std::layout_right, accessor>;
    proxy_span x(data, proxy_span::mapping_type{}, accessor{&products, &conversions});

    static_assert(std::is_same_v<decltype(std::linalg::dot(x, x)), float>);
    static_assert(std::is_same_v<decltype(std::linalg::dotc(x, x)), float>);
    EXPECT_EQ(std::linalg::dot(x, x), 0.0f);
    EXPECT_EQ(std::linalg::dotc(x, x), 0.0f);
    EXPECT_EQ(std::linalg::dot(x, x, 7.0), 7.0);
    EXPECT_EQ(std::linalg::dotc(x, x, 7.0), 7.0);
    EXPECT_EQ(products, 0);
    EXPECT_EQ(conversions, 0);
}

TEST(LinalgLevel1DotProducts, ProxyValueTypeUsesWiderInitBeforeMultiplication) {
    float x_data[] = {1.0e20f};
    float y_data[] = {1.0e20f};
    int products = 0;
    int conversions = 0;
    using proxy_span = std::mdspan<const float, std::extents<int, 1>,
                                   std::layout_right, dot_proxy_accessor<float>>;
    const dot_proxy_accessor<float> accessor{&products, &conversions};
    proxy_span x(x_data, proxy_span::mapping_type{}, accessor);
    proxy_span y(y_data, proxy_span::mapping_type{}, accessor);
    const double expected = static_cast<double>(x_data[0]) *
                            static_cast<double>(y_data[0]);

    const double dot = std::linalg::dot(x, y, 0.0);
    EXPECT_TRUE(std::isfinite(dot));
    EXPECT_EQ(dot, expected);
    EXPECT_NEAR(dot, 1.0e40, 1.0e34);
    EXPECT_EQ(products, 0);
    EXPECT_EQ(conversions, 2);

    conversions = 0;
    const double dotc = std::linalg::dotc(x, y, 0.0);
    EXPECT_TRUE(std::isfinite(dotc));
    EXPECT_EQ(dotc, expected);
    EXPECT_NEAR(dotc, 1.0e40, 1.0e34);
    EXPECT_EQ(products, 0);
    EXPECT_EQ(conversions, 2);
}

TEST(LinalgLevel1DotProducts, ProxyWideningKeepsBinaryTermsAndSamePrecisionMultiply) {
    float x_data[] = {1.0f + 0x1p-23f};
    float y_data[] = {1.0f - 0x1p-23f};
    int products = 0;
    int conversions = 0;
    using proxy_span = std::mdspan<const float, std::extents<int, 1>,
                                   std::layout_right, dot_proxy_accessor<float>>;
    const dot_proxy_accessor<float> accessor{&products, &conversions};
    proxy_span x(x_data, proxy_span::mapping_type{}, accessor);
    proxy_span y(y_data, proxy_span::mapping_type{}, accessor);

    EXPECT_EQ(std::linalg::dot(x, y, 0.0), 1.0 - 0x1p-46);
    EXPECT_EQ(products, 0);
    EXPECT_EQ(conversions, 2);

    conversions = 0;
    EXPECT_EQ(std::linalg::dotc(x, y, 0.0), 1.0 - 0x1p-46);
    EXPECT_EQ(products, 0);
    EXPECT_EQ(conversions, 2);

    conversions = 0;
    EXPECT_EQ(std::linalg::dot(x, y, 0.0f), 1.0f);
    EXPECT_EQ(products, 1);
    EXPECT_EQ(conversions, 0);

    products = 0;
    EXPECT_EQ(std::linalg::dotc(x, y, 0.0f), 1.0f);
    EXPECT_EQ(products, 1);
    EXPECT_EQ(conversions, 0);

    products = 0;
    EXPECT_EQ(std::linalg::dot(x, y), 1.0f);
    EXPECT_EQ(products, 1);
    EXPECT_EQ(conversions, 0);

    products = 0;
    EXPECT_EQ(std::linalg::dotc(x, y), 1.0f);
    EXPECT_EQ(products, 1);
    EXPECT_EQ(conversions, 0);
}

TEST(LinalgLevel1Reductions, ExplicitInitControlsReturnType) {
    double x_data[] = {3.0, 4.0};
    double y_data[] = {5.0, 6.0};
    double matrix_data[] = {1.0, -2.0, 3.0, -4.0};
    std::mdspan x(x_data, std::extents<int, 2>{});
    std::mdspan y(y_data, std::extents<int, 2>{});
    std::mdspan matrix(matrix_data, std::extents<int, 2, 2>{});

    static_assert(std::is_same_v<decltype(std::linalg::dot(x, y, 0.0f)), float>);
    static_assert(std::is_same_v<decltype(std::linalg::dotc(x, y, 0.0)), double>);
    static_assert(std::is_same_v<decltype(std::linalg::vector_two_norm(x, 0.0f)), float>);
    static_assert(std::is_same_v<decltype(std::linalg::vector_abs_sum(x, 0.0)), double>);
    static_assert(std::is_same_v<decltype(std::linalg::matrix_frob_norm(matrix, 0.0f)), float>);
    static_assert(std::is_same_v<decltype(std::linalg::matrix_one_norm(matrix, 0.0)), double>);
    static_assert(std::is_same_v<decltype(std::linalg::matrix_inf_norm(matrix, 0.0)), double>);
}

TEST(LinalgNorms, InitializedNormsAcceptRvalueMagnitudeProducts) {
    using linalg_norm_test::complex;
    const complex data[]{complex{3.0}, complex{0.0, 4.0}};
    std::mdspan vector(data, std::extents<int, 2>{});
    std::mdspan matrix(data, std::extents<int, 1, 2>{});

    EXPECT_DOUBLE_EQ(std::linalg::vector_two_norm(vector, 12.0), 13.0);
    EXPECT_DOUBLE_EQ(std::linalg::matrix_frob_norm(matrix, 12.0), 13.0);
#if FORGE_LINALG_HAS_EXECUTION_POLICY_OVERLOADS
    EXPECT_DOUBLE_EQ(std::linalg::vector_two_norm(std::execution::seq, vector, 12.0), 13.0);
    EXPECT_DOUBLE_EQ(std::linalg::matrix_frob_norm(std::execution::seq, matrix, 12.0), 13.0);
#endif
}

TEST(LinalgLevel1Reductions, VectorTwoNormCombinesInitEuclideanly) {
    double x_data[] = {4.0};
    std::mdspan x(x_data, std::extents<int, 1>{});

    EXPECT_DOUBLE_EQ(std::linalg::vector_two_norm(x, 3.0), 5.0);
}

TEST(LinalgLevel1Reductions, VectorTwoNormAvoidsIntermediateOverflow) {
    double x_data[] = {1.0e200, 1.0e200};
    std::mdspan x(x_data, std::extents<int, 2>{});

    const double expected = std::sqrt(2.0) * 1.0e200;
    EXPECT_NEAR(std::linalg::vector_two_norm(x), expected, 1.0e185);
}

// abs() of the most negative signed value is undefined; the magnitude
// helpers compute it in the unsigned counterpart instead.
TEST(LinalgLevel1Reductions, SignedMinimumMagnitudeIsWellDefined) {
    constexpr int int_min = std::numeric_limits<int>::min();
    int x_data[] = {int_min};
    std::mdspan x(x_data, std::extents<int, 1>{});

    EXPECT_EQ(std::linalg::vector_abs_sum(x, std::int64_t{0}),
              std::int64_t{2147483648});
    static_assert(std::is_same_v<
                  decltype(std::linalg::vector_abs_sum(x)),
                  int>);
    EXPECT_EQ(std::linalg::vector_abs_sum(x),
              std::numeric_limits<int>::max());

    int accumulated_data[] = {int_min, 1};
    std::mdspan accumulated(
        accumulated_data, std::extents<int, 2>{});
    EXPECT_EQ(
        std::linalg::vector_abs_sum(accumulated, std::int64_t{0}),
        std::int64_t{2147483649});
    EXPECT_EQ(
        std::linalg::vector_abs_sum(accumulated, 0),
        std::numeric_limits<int>::max());

    int y_data[] = {5, int_min, 7};
    std::mdspan y(y_data, std::extents<int, 3>{});
    EXPECT_EQ(std::linalg::vector_idx_abs_max(y),
              decltype(y)::size_type{1});

    int matrix_data[] = {int_min, int_min, 1, 2};
    std::mdspan matrix(matrix_data, std::extents<int, 2, 2>{});
    static_assert(std::is_same_v<
                  decltype(std::linalg::matrix_one_norm(matrix)),
                  unsigned>);
    static_assert(std::is_same_v<
                  decltype(std::linalg::matrix_inf_norm(matrix)),
                  unsigned>);
    EXPECT_EQ(
        std::linalg::matrix_one_norm(matrix, std::int64_t{0}),
        std::int64_t{2147483650});
    EXPECT_EQ(
        std::linalg::matrix_inf_norm(matrix, std::int64_t{0}),
        std::int64_t{4294967296});
    EXPECT_EQ(
        std::linalg::matrix_one_norm(matrix),
        2147483650u);
    EXPECT_EQ(
        std::linalg::matrix_inf_norm(matrix),
        std::numeric_limits<unsigned>::max());
    EXPECT_EQ(
        std::linalg::matrix_one_norm(matrix, 0),
        std::numeric_limits<int>::max());
    EXPECT_EQ(
        std::linalg::matrix_inf_norm(matrix, 0),
        std::numeric_limits<int>::max());
}

TEST(LinalgLevel1Reductions, UnsignedAbsSumSaturates) {
    constexpr unsigned u_max = std::numeric_limits<unsigned>::max();
    unsigned unsigned_data[] = {u_max, 1u};
    std::mdspan unsigned_vector(
        unsigned_data, std::extents<int, 2>{});
    EXPECT_EQ(
        std::linalg::vector_abs_sum(unsigned_vector),
        u_max);
}

TEST(LinalgLevel1Reductions, IntegralSumsRemainExactAboveDoubleRange) {
    constexpr std::uint64_t exact = (std::uint64_t{1} << 53) + 1;
    std::uint64_t vector_data[] = {exact};
    std::uint64_t matrix_data[] = {exact};
    std::mdspan vector(vector_data, std::extents<int, 1>{});
    std::mdspan matrix(matrix_data, std::extents<int, 1, 1>{});

    EXPECT_EQ(std::linalg::vector_abs_sum(vector), exact);
    EXPECT_EQ(std::linalg::vector_abs_sum(vector, std::uint64_t{0}), exact);
    EXPECT_EQ(std::linalg::matrix_one_norm(matrix), exact);
    EXPECT_EQ(std::linalg::matrix_inf_norm(matrix), exact);
}

TEST(LinalgLevel1Reductions, Int8NegativeInitMagnitudeBoundaries) {
    struct test_case {
        std::int8_t init;
        std::uintmax_t magnitude;
        int expected;
    };
    constexpr auto minimum = std::numeric_limits<std::int8_t>::min();
    constexpr auto largest_magnitude = std::numeric_limits<std::uintmax_t>::max();
    constexpr test_case cases[] = {
        {-1, 0, -1},
        {-1, 1, 0},
        {-1, 2, 1},
        {-1, 127, 126},
        {-1, 128, 127},
        {-1, 129, 127},
        {-1, 200, 127},
        {-1, largest_magnitude, 127},
        {minimum, 0, -128},
        {minimum, 1, -127},
        {minimum, 127, -1},
        {minimum, 128, 0},
        {minimum, 129, 1},
        {minimum, 254, 126},
        {minimum, 255, 127},
        {minimum, 256, 127},
        {minimum, largest_magnitude, 127},
    };

    for (const auto& entry : cases) {
        SCOPED_TRACE(testing::Message()
                     << "init=" << static_cast<int>(entry.init)
                     << ", magnitude=" << entry.magnitude);
        std::uintmax_t data[] = {entry.magnitude};
        std::mdspan vector(data, std::extents<int, 1>{});
        std::mdspan matrix(data, std::extents<int, 1, 1>{});

        static_assert(std::is_same_v<
                      decltype(std::linalg::vector_abs_sum(vector, entry.init)),
                      std::int8_t>);
        static_assert(std::is_same_v<
                      decltype(std::linalg::matrix_one_norm(matrix, entry.init)),
                      std::int8_t>);
        static_assert(std::is_same_v<
                      decltype(std::linalg::matrix_inf_norm(matrix, entry.init)),
                      std::int8_t>);
        EXPECT_EQ(std::linalg::vector_abs_sum(vector, entry.init), entry.expected);
        EXPECT_EQ(std::linalg::matrix_one_norm(matrix, entry.init), entry.expected);
        EXPECT_EQ(std::linalg::matrix_inf_norm(matrix, entry.init), entry.expected);
    }
}

TEST(LinalgLevel1Reductions, Int16NegativeInitMagnitudeBoundaries) {
    struct test_case {
        std::int16_t init;
        std::uintmax_t magnitude;
        int expected;
    };
    constexpr auto minimum = std::numeric_limits<std::int16_t>::min();
    constexpr auto largest_magnitude = std::numeric_limits<std::uintmax_t>::max();
    constexpr test_case cases[] = {
        {-1, 0, -1},
        {-1, 1, 0},
        {-1, 2, 1},
        {-1, 32767, 32766},
        {-1, 32768, 32767},
        {-1, 32769, 32767},
        {-1, 40000, 32767},
        {-1, largest_magnitude, 32767},
        {minimum, 0, -32768},
        {minimum, 1, -32767},
        {minimum, 32767, -1},
        {minimum, 32768, 0},
        {minimum, 32769, 1},
        {minimum, 65534, 32766},
        {minimum, 65535, 32767},
        {minimum, 65536, 32767},
        {minimum, largest_magnitude, 32767},
    };

    for (const auto& entry : cases) {
        SCOPED_TRACE(testing::Message()
                     << "init=" << static_cast<int>(entry.init)
                     << ", magnitude=" << entry.magnitude);
        std::uintmax_t data[] = {entry.magnitude};
        std::mdspan vector(data, std::extents<int, 1>{});
        std::mdspan matrix(data, std::extents<int, 1, 1>{});

        static_assert(std::is_same_v<
                      decltype(std::linalg::vector_abs_sum(vector, entry.init)),
                      std::int16_t>);
        static_assert(std::is_same_v<
                      decltype(std::linalg::matrix_one_norm(matrix, entry.init)),
                      std::int16_t>);
        static_assert(std::is_same_v<
                      decltype(std::linalg::matrix_inf_norm(matrix, entry.init)),
                      std::int16_t>);
        EXPECT_EQ(std::linalg::vector_abs_sum(vector, entry.init), entry.expected);
        EXPECT_EQ(std::linalg::matrix_one_norm(matrix, entry.init), entry.expected);
        EXPECT_EQ(std::linalg::matrix_inf_norm(matrix, entry.init), entry.expected);
    }
}

TEST(LinalgLevel1Reductions, WiderScalarControlsComplexMagnitudePrecision) {
    using complex = std::complex<float>;
    const float largest = std::numeric_limits<float>::max();
    complex vector_data[] = {{largest, largest}};
    complex matrix_data[] = {{largest, largest}};
    std::mdspan vector(vector_data, std::extents<int, 1>{});
    std::mdspan matrix(matrix_data, std::extents<int, 1, 1>{});

    const double magnitude = std::hypot(
        static_cast<double>(largest), static_cast<double>(largest));
    EXPECT_DOUBLE_EQ(
        std::linalg::vector_abs_sum(vector, 0.0),
        2.0 * static_cast<double>(largest));
    EXPECT_DOUBLE_EQ(std::linalg::vector_two_norm(vector, 0.0), magnitude);
    EXPECT_DOUBLE_EQ(std::linalg::matrix_frob_norm(matrix, 0.0), magnitude);
    EXPECT_DOUBLE_EQ(std::linalg::matrix_one_norm(matrix, 0.0), magnitude);
    EXPECT_DOUBLE_EQ(std::linalg::matrix_inf_norm(matrix, 0.0), magnitude);
}

template<class T>
void expect_within_ulps(T actual, T expected, int ulps) {
    ASSERT_TRUE(std::isfinite(actual));
    T lower = expected;
    T upper = expected;
    const T infinity = std::numeric_limits<T>::infinity();
    for (int i = 0; i < ulps; ++i) {
        lower = std::nextafter(lower, -infinity);
        upper = std::nextafter(upper, infinity);
    }
    EXPECT_GE(actual, lower);
    EXPECT_LE(actual, upper);
}

struct complex_float_norm_accessor : rotation_accessor<std::complex<float>> {
    using offset_policy = complex_float_norm_accessor;
    struct reference : rotation_reference<std::complex<float>> {
        friend float abs(reference value) noexcept { return std::abs(*value.pointer); }
    };
    reference access(data_handle_type p, std::size_t i) const noexcept { return {{p + i}}; }
};

struct wider_norm_accessor {
    using element_type = const float;
    using reference = double;
    using data_handle_type = const double*;
    using offset_policy = wider_norm_accessor;
    reference access(data_handle_type p, std::size_t i) const noexcept { return p[i]; }
    data_handle_type offset(data_handle_type p, std::size_t i) const noexcept { return p + i; }
};

struct wider_complex_norm_accessor : wider_norm_accessor {
    using element_type = const std::complex<float>;
    using offset_policy = wider_complex_norm_accessor;
    struct reference {
        double real;
        operator std::complex<float>() const noexcept { return {static_cast<float>(real), 0.0f}; }
        friend double abs(reference value) noexcept { return std::abs(value.real); }
    };
    reference access(data_handle_type p, std::size_t i) const noexcept { return {p[i]}; }
};

struct wider_real_norm_accessor {
    using element_type = const double;
    using offset_policy = wider_real_norm_accessor;
    using data_handle_type = const long double*;
    struct reference {
        long double value;
        operator double() const noexcept { return static_cast<double>(value); }
        friend long double abs(reference x) noexcept { return std::abs(x.value); }
    };
    reference access(data_handle_type p, std::size_t i) const noexcept { return {p[i]}; }
    data_handle_type offset(data_handle_type p, std::size_t i) const noexcept { return p + i; }
};

template<class Accessor>
void check_proxy_norms(typename Accessor::data_handle_type data, double expected) {
    using element_type = typename Accessor::element_type;
    using vector = std::mdspan<element_type, std::extents<int, 1>, std::layout_right, Accessor>;
    using matrix = std::mdspan<element_type, std::extents<int, 1, 1>, std::layout_right, Accessor>;
    const vector v(data, typename vector::mapping_type{}, Accessor{});
    const matrix m(data, typename matrix::mapping_type{}, Accessor{});
    expect_within_ulps(std::linalg::vector_two_norm(v, 0.0), expected, 1);
    expect_within_ulps(std::linalg::matrix_frob_norm(m, 0.0), expected, 1);
}

TEST(LinalgLevel1Reductions, ComplexProxyWidensBeforeMeasuringMagnitude) {
    std::complex<float> data{1.0f, 1.0f};
    check_proxy_norms<complex_float_norm_accessor>(&data, 0x1.6a09e667f3bcdp+0);
    data = {4.0f, std::nextafter(3.0f, 0.0f)};
    check_proxy_norms<complex_float_norm_accessor>(&data, 0x1.3fffff666666ap+2);
}

TEST(LinalgLevel1Reductions, ComplexProxyKeepsWiderAdlMagnitude) {
    constexpr double data = 0x1.3fffffff00000p+2; // 5 - 2^-30
    check_proxy_norms<wider_complex_norm_accessor>(&data, data);
}

TEST(LinalgLevel1Reductions, RealProxyKeepsWiderAdlMagnitude) {
    constexpr long double data = 5.0L - 0x1p-60L;
    if constexpr (std::numeric_limits<long double>::digits >
                  std::numeric_limits<double>::digits && data < 5.0L) {
        check_proxy_norms<wider_real_norm_accessor>(&data, 5.0);
    }
}

TEST(LinalgLevel1Reductions, MixedPrecisionNormsKeepTheWiderAccessorRead) {
    constexpr double data = 0x1.3fffffff00000p+2; // 5 - 2^-30
    check_proxy_norms<wider_norm_accessor>(&data, data);

    using vector = std::mdspan<const float, std::extents<int, 1>,
                               std::layout_right, wider_norm_accessor>;
    using matrix = std::mdspan<const float, std::extents<int, 1, 1>,
                               std::layout_right, wider_norm_accessor>;
    const vector v(&data, vector::mapping_type{}, wider_norm_accessor{});
    const matrix m(&data, matrix::mapping_type{}, wider_norm_accessor{});
    static_assert(std::same_as<decltype(std::linalg::vector_two_norm(v)), float>);
    static_assert(std::same_as<decltype(std::linalg::matrix_frob_norm(m)), float>);
    EXPECT_EQ(std::linalg::vector_two_norm(v), 5.0f);
    EXPECT_EQ(std::linalg::matrix_frob_norm(m), 5.0f);
}

TEST(LinalgLevel1Reductions, NormsReadProxiesWithoutAdlAbs) {
    using accessor = rotation_accessor<double>;
    using vector = std::mdspan<double, std::extents<int, 2>, std::layout_right, accessor>;
    using matrix = std::mdspan<double, std::extents<int, 1, 2>, std::layout_right, accessor>;
    double data[]{3.0, 4.0};
    const vector v(data, vector::mapping_type{}, accessor{});
    const matrix m(data, matrix::mapping_type{}, accessor{});
    EXPECT_EQ(std::linalg::vector_two_norm(v), 5.0);
    EXPECT_EQ(std::linalg::matrix_frob_norm(m), 5.0);
    EXPECT_EQ(std::linalg::vector_two_norm(v, 0.0), 5.0);
    EXPECT_EQ(std::linalg::matrix_frob_norm(m, 0.0), 5.0);
}

TEST(LinalgLevel1Reductions, ComplexInitUsesMagnitudeSquared) {
    using complex = std::complex<double>;
    double x_data[] = {4.0};
    std::mdspan x(x_data, std::extents<int, 1>{});
    const complex init{0.0, 3.0};
    const complex expected{5.0, 0.0};
    EXPECT_EQ(std::linalg::vector_two_norm(x, init), expected);
}

TEST(LinalgLevel1Norm, DoubleEquivalence) {
    constexpr int N = 1024;
    static double x[N];
    for (int i = 0; i < N; ++i) x[i] = 1.0;
    std::mdspan xv(x, std::extents<int, N>{});
    const double expected = std::sqrt(static_cast<double>(N));
    EXPECT_NEAR(std::linalg::vector_two_norm(xv), expected, 1e-10);
}

TEST(LinalgLevel1AsumSIMD, DoubleEquivalence) {
    constexpr int N = 512;
    static double x[N];
    for (int i = 0; i < N; ++i) x[i] = (i % 2 == 0) ? 1.0 : -1.0;
    std::mdspan xv(x, std::extents<int, N>{});
    double asum = std::linalg::vector_abs_sum(xv);
    EXPECT_NEAR(asum, static_cast<double>(N), 1e-10);
}

TEST(LinalgLevel1AsumSIMD, FloatEquivalence) {
    constexpr int N = 512;
    static float x[N];
    for (int i = 0; i < N; ++i) x[i] = (i % 2 == 0) ? 1.0f : -1.0f;
    std::mdspan xv(x, std::extents<int, N>{});
    float asum = std::linalg::vector_abs_sum(xv);
    EXPECT_NEAR(asum, static_cast<float>(N), 1e-4f);
}

TEST(LinalgLevel1Asum, ComplexUsesOneNorm) {
    using complex = std::complex<double>;
    complex data[] = {{3.0, 4.0}, {-1.0, 2.0}};
    std::mdspan v(data, std::extents<int, 2>{});

    static_assert(std::is_same_v<
                  decltype(std::linalg::vector_abs_sum(v)),
                  complex>);
    EXPECT_EQ(std::linalg::vector_abs_sum(v), complex(10.0, 0.0));
    EXPECT_DOUBLE_EQ(std::linalg::vector_abs_sum(v, 1.0), 11.0);
}

TEST(LinalgLevel1IdxAbsMax, HandlesEmptyTiesAndNegativeValues) {
    double empty_data[] = {0.0};
    std::mdspan empty(empty_data, std::extents<int, 0>{});
    using empty_view_t = decltype(empty);
    static_assert(std::is_same_v<
                  decltype(std::linalg::vector_idx_abs_max(empty)),
                  typename empty_view_t::size_type>);
    EXPECT_EQ(
        std::linalg::vector_idx_abs_max(empty),
        std::numeric_limits<typename empty_view_t::size_type>::max());

    double data[] = {-5.0, 2.0, 5.0, -4.0};
    std::mdspan v(data, std::extents<int, 4>{});
    EXPECT_EQ(
        std::linalg::vector_idx_abs_max(v),
        typename decltype(v)::size_type{0});
}

TEST(LinalgLevel1IdxAbsMax, ComplexUsesOneNormTerm) {
    using complex = std::complex<double>;
    complex data[] = {{3.0, 4.0}, {5.5, 0.0}};
    std::mdspan v(data, std::extents<int, 2>{});

    EXPECT_EQ(std::linalg::vector_idx_abs_max(v), 0);
}

TEST(LinalgLevel1Reductions, UnsignedMagnitudesRemainWellFormed) {
    unsigned vector_data[] = {1u, 4u, 2u};
    unsigned matrix_data[] = {1u, 2u, 3u, 4u};
    std::mdspan vector(vector_data, std::extents<int, 3>{});
    std::mdspan matrix(matrix_data, std::extents<int, 2, 2>{});

    EXPECT_EQ(std::linalg::vector_abs_sum(vector), 7u);
    EXPECT_EQ(std::linalg::vector_idx_abs_max(vector), 1u);
    EXPECT_EQ(std::linalg::matrix_one_norm(matrix), 6u);
    EXPECT_EQ(std::linalg::matrix_inf_norm(matrix), 7u);
}

TEST(LinalgLevel1MatrixNorms, RectangularRealMatrix) {
    double data[] = {1.0, -2.0, 3.0, -4.0, 5.0, -6.0};
    std::mdspan matrix(data, std::extents<int, 2, 3>{});

    EXPECT_NEAR(std::linalg::matrix_frob_norm(matrix), std::sqrt(91.0), 1e-12);
    EXPECT_DOUBLE_EQ(std::linalg::matrix_one_norm(matrix), 9.0);
    EXPECT_DOUBLE_EQ(std::linalg::matrix_inf_norm(matrix), 15.0);
}

TEST(LinalgLevel1MatrixNorms, OneNormAddsExplicitInit) {
    double data[] = {1.0, -2.0, 3.0, -4.0, 5.0, -6.0};
    std::mdspan matrix(data, std::extents<int, 2, 3>{});

    EXPECT_DOUBLE_EQ(std::linalg::matrix_one_norm(matrix, 10.0), 19.0);
}

TEST(LinalgLevel1MatrixNorms, InfinityNormAddsExplicitInit) {
    double data[] = {1.0, -2.0, 3.0, -4.0, 5.0, -6.0};
    std::mdspan matrix(data, std::extents<int, 2, 3>{});

    EXPECT_DOUBLE_EQ(std::linalg::matrix_inf_norm(matrix, 10.0), 25.0);
}

TEST(LinalgLevel1MatrixNorms, ComplexMatrixUsesMagnitude) {
    using complex = std::complex<double>;
    complex data[] = {{3.0, 4.0}, {0.0, 0.0}, {1.0, -1.0}, {-2.0, 0.0}};
    std::mdspan matrix(data, std::extents<int, 2, 2>{});

    static_assert(std::is_same_v<decltype(std::linalg::matrix_frob_norm(matrix)), double>);
    static_assert(std::is_same_v<decltype(std::linalg::matrix_one_norm(matrix)), double>);
    static_assert(std::is_same_v<decltype(std::linalg::matrix_inf_norm(matrix)), double>);
    EXPECT_NEAR(std::linalg::matrix_frob_norm(matrix), std::sqrt(31.0), 1e-12);
    EXPECT_NEAR(std::linalg::matrix_one_norm(matrix), 5.0 + std::sqrt(2.0), 1e-12);
    EXPECT_NEAR(std::linalg::matrix_inf_norm(matrix), 5.0, 1e-12);
}

TEST(LinalgLevel1MatrixNorms, OneAndInfinityNormAcceptComplexInit) {
    using complex = std::complex<double>;
    complex data[] = {{3.0, 4.0}, {0.0, 0.0}, {1.0, -1.0}, {-2.0, 0.0}};
    std::mdspan matrix(data, std::extents<int, 2, 2>{});
    const complex init{1.0, 2.0};

    EXPECT_EQ(
        std::linalg::matrix_one_norm(matrix, init),
        (complex{6.0 + std::sqrt(2.0), 2.0}));
    EXPECT_EQ(
        std::linalg::matrix_inf_norm(matrix, init),
        (complex{6.0, 2.0}));
}

TEST(LinalgLevel1MatrixNorms, FrobeniusInitUsesMagnitudeSquared) {
    using complex = std::complex<double>;
    complex data[] = {{4.0, 0.0}};
    std::mdspan matrix(data, std::extents<int, 1, 1>{});

    const complex init{0.0, 3.0};
    const complex expected{5.0, 0.0};
    EXPECT_EQ(std::linalg::matrix_frob_norm(matrix, init), expected);
}

TEST(LinalgLevel1MatrixNorms, FrobeniusSquaresElementsInAccumulationType) {
    float data[] = {1.0e20f};
    std::mdspan matrix(data, std::extents<int, 1, 1>{});

    EXPECT_NEAR(std::linalg::matrix_frob_norm(matrix, 0.0), 1.0e20, 1.0e14);
}

TEST(LinalgLevel1MatrixNorms, FrobeniusNormAvoidsIntermediateOverflow) {
    double data[] = {1.0e200, 1.0e200};
    std::mdspan matrix(data, std::extents<int, 1, 2>{});

    const double expected = std::sqrt(2.0) * 1.0e200;
    EXPECT_NEAR(std::linalg::matrix_frob_norm(matrix), expected, 1.0e185);
}

TEST(LinalgLevel1Givens, SetupAndApplyRotation) {
    auto rotation = std::linalg::setup_givens_rotation(3.0, 4.0);
    EXPECT_DOUBLE_EQ(rotation.r, 5.0);
    EXPECT_DOUBLE_EQ(rotation.c, 0.6);
    EXPECT_DOUBLE_EQ(rotation.s, 0.8);

    double x_data[] = {3.0, 0.0};
    double y_data[] = {4.0, 2.0};
    std::mdspan x(x_data, std::extents<int, 2>{});
    std::mdspan y(y_data, std::extents<int, 2>{});
    std::linalg::apply_givens_rotation(x, y, rotation.c, rotation.s);

    EXPECT_NEAR(x[0], 5.0, 1e-12);
    EXPECT_NEAR(y[0], 0.0, 1e-12);
    EXPECT_NEAR(x[1], 1.6, 1e-12);
    EXPECT_NEAR(y[1], 1.2, 1e-12);
}

TEST(LinalgLevel1Givens, DegenerateRealRotationsKeepNormNonnegative) {
    const auto check = [](double a, double b, double expected_r) {
        const auto rotation = std::linalg::setup_givens_rotation(a, b);
        EXPECT_DOUBLE_EQ(rotation.r, expected_r);
        EXPECT_DOUBLE_EQ(rotation.c * a + rotation.s * b, rotation.r);
        EXPECT_DOUBLE_EQ(-rotation.s * a + rotation.c * b, 0.0);
    };

    check(3.0, 0.0, 3.0);
    check(-3.0, 0.0, 3.0);
    check(0.0, 4.0, 4.0);
    check(0.0, -4.0, 4.0);
    check(0.0, 0.0, 0.0);
}

TEST(LinalgLevel1Givens, ComplexRotationUsesRealCAndConjugateS) {
    using complex = std::complex<double>;
    const auto rotation = std::linalg::setup_givens_rotation(complex{1.0, 0.0}, complex{0.0, 1.0});
    const double inv_sqrt2 = 1.0 / std::sqrt(2.0);
    EXPECT_NEAR(rotation.c, inv_sqrt2, 1e-12);
    EXPECT_NEAR(rotation.s.real(), 0.0, 1e-12);
    EXPECT_NEAR(rotation.s.imag(), -inv_sqrt2, 1e-12);
    EXPECT_NEAR(rotation.r.real(), std::sqrt(2.0), 1e-12);
    EXPECT_NEAR(rotation.r.imag(), 0.0, 1e-12);

    complex x_data[] = {{1.0, 0.0}};
    complex y_data[] = {{0.0, 1.0}};
    std::mdspan x(x_data, std::extents<int, 1>{});
    std::mdspan y(y_data, std::extents<int, 1>{});
    std::linalg::apply_givens_rotation(x, y, rotation.c, rotation.s);

    EXPECT_NEAR(x[0].real(), std::sqrt(2.0), 1e-12);
    EXPECT_NEAR(x[0].imag(), 0.0, 1e-12);
    EXPECT_NEAR(y[0].real(), 0.0, 1e-12);
    EXPECT_NEAR(y[0].imag(), 0.0, 1e-12);
}

TEST(LinalgLevel1Givens, ComplexZeroFirstInputUsesRealNormAndConjugatePhase) {
    using complex = std::complex<double>;
    const auto check = [](complex b, complex expected_s, double expected_r) {
        SCOPED_TRACE(b);
        const auto rotation = std::linalg::setup_givens_rotation(complex{}, b);
        EXPECT_DOUBLE_EQ(rotation.c, 0.0);
        EXPECT_NEAR(rotation.s.real(), expected_s.real(), 1e-12);
        EXPECT_NEAR(rotation.s.imag(), expected_s.imag(), 1e-12);
        EXPECT_DOUBLE_EQ(rotation.r.real(), expected_r);
        EXPECT_DOUBLE_EQ(rotation.r.imag(), 0.0);
        EXPECT_NEAR(std::norm(rotation.s), 1.0, 1e-12);

        complex x_data[] = {complex{}};
        complex y_data[] = {b};
        std::mdspan x(x_data, std::extents<int, 1>{});
        std::mdspan y(y_data, std::extents<int, 1>{});
        std::linalg::apply_givens_rotation(x, y, rotation.c, rotation.s);
        EXPECT_NEAR(x[0].real(), expected_r, 1e-12);
        EXPECT_NEAR(x[0].imag(), 0.0, 1e-12);
        EXPECT_EQ(y[0], complex{});
    };

    check(complex{3.0, 4.0}, complex{0.6, -0.8}, 5.0);
    check(complex{0.0, -4.0}, complex{0.0, 1.0}, 4.0);
    check(complex{-5.0, 0.0}, complex{-1.0, 0.0}, 5.0);
}

TEST(LinalgLevel1Givens, ComplexZeroInputsKeepIdentity) {
    using complex = std::complex<double>;
    const auto rotation = std::linalg::setup_givens_rotation(complex{}, complex{});
    EXPECT_DOUBLE_EQ(rotation.c, 1.0);
    EXPECT_EQ(rotation.s, complex{});
    EXPECT_EQ(rotation.r, complex{});
}

TEST(LinalgLevel1Givens, ComplexRotationPreservesNonzeroInputPhase) {
    using complex = std::complex<double>;
    const complex a{0.0, 3.0};
    const complex b{4.0, 0.0};
    const auto rotation = std::linalg::setup_givens_rotation(a, b);
    EXPECT_NEAR(rotation.c, 0.6, 1e-12);
    EXPECT_NEAR(rotation.s.real(), 0.0, 1e-12);
    EXPECT_NEAR(rotation.s.imag(), 0.8, 1e-12);
    EXPECT_NEAR(rotation.r.real(), 0.0, 1e-12);
    EXPECT_NEAR(rotation.r.imag(), 5.0, 1e-12);
    EXPECT_NEAR(std::abs(rotation.c * a + rotation.s * b - rotation.r), 0.0, 1e-12);
    EXPECT_NEAR(std::abs(-std::conj(rotation.s) * a + rotation.c * b), 0.0, 1e-12);

    const auto identity = std::linalg::setup_givens_rotation(a, complex{});
    EXPECT_DOUBLE_EQ(identity.c, 1.0);
    EXPECT_EQ(identity.s, complex{});
    EXPECT_EQ(identity.r, a);
}

TEST(LinalgLevel1Givens, ComplexZeroFirstInputNormalizesExtremeFinitePhase) {
    using complex = std::complex<double>;
    struct case_t {
        complex b;
        complex s;
        double r;
        double scale;
    };
    const double sqrt2 = std::sqrt(2.0);
    const double inv_sqrt2 = 1.0 / sqrt2;
    const double huge = 0.6 * std::numeric_limits<double>::max();
    const double tiny = std::numeric_limits<double>::min();
    const double subnormal = std::numeric_limits<double>::denorm_min();
    const std::array cases{
        case_t{{huge, huge}, {inv_sqrt2, -inv_sqrt2}, huge * sqrt2, huge},
        case_t{{1.7976931146382683e308, 2.696539671957402e304},
               {0.99999998875, -0.0001499999983125},
               std::numeric_limits<double>::max(), 1.7976931146382683e308},
        case_t{{tiny, -tiny}, {inv_sqrt2, inv_sqrt2}, tiny * sqrt2, tiny},
        case_t{{subnormal, subnormal}, {inv_sqrt2, -inv_sqrt2}, subnormal, subnormal},
        case_t{{3.0 * subnormal, -4.0 * subnormal}, {0.6, 0.8},
               5.0 * subnormal, subnormal},
    };
    for (const auto& test_case : cases) {
        SCOPED_TRACE(test_case.b);
        const auto rotation = std::linalg::setup_givens_rotation(complex{}, test_case.b);
        EXPECT_DOUBLE_EQ(rotation.c, 0.0);
        EXPECT_NEAR(rotation.s.real(), test_case.s.real(), 1e-12);
        EXPECT_NEAR(rotation.s.imag(), test_case.s.imag(), 1e-12);
        EXPECT_NEAR(std::norm(rotation.s), 1.0, 1e-12);
        EXPECT_TRUE(std::isfinite(rotation.r.real()));
        EXPECT_DOUBLE_EQ(rotation.r.real(), test_case.r);
        EXPECT_NEAR(rotation.r.real() / test_case.r, 1.0, 1e-12);
        EXPECT_DOUBLE_EQ(rotation.r.imag(), 0.0);

        const complex scaled_b{test_case.b.real() / test_case.scale,
                               test_case.b.imag() / test_case.scale};
        const complex rotated = rotation.s * scaled_b;
        EXPECT_NEAR(rotated.real(), std::abs(scaled_b), 1e-12);
        EXPECT_NEAR(rotated.imag(), 0.0, 1e-12);
    }
}

TEST(LinalgLevel1Givens, ComplexRotationAvoidsFiniteScaleOverflow) {
    using complex = std::complex<double>;
    const double component = 0.6 * std::numeric_limits<double>::max();
    const auto rotation = std::linalg::setup_givens_rotation(
        complex{component, 0.0}, complex{component, 0.0});

    EXPECT_TRUE(std::isfinite(rotation.c));
    EXPECT_TRUE(std::isfinite(rotation.s.real()));
    EXPECT_TRUE(std::isfinite(rotation.r.real()));
    EXPECT_NEAR(rotation.c, 1.0 / std::sqrt(2.0), 1e-12);
    EXPECT_NEAR(rotation.s.real(), 1.0 / std::sqrt(2.0), 1e-12);
    EXPECT_NEAR(rotation.r.real() / component, std::sqrt(2.0), 1e-12);
}

template<class T>
class LinalgLevel1ExtremeNumbers : public testing::Test {};

using extreme_number_types = testing::Types<float, double>;
TYPED_TEST_SUITE(LinalgLevel1ExtremeNumbers, extreme_number_types);

template<class T>
constexpr T inverse_sqrt_two = static_cast<T>(0x1.6a09e667f3bcdp-1);

template<class T>
constexpr T large_component = T{0.625} * std::numeric_limits<T>::max();

template<class T>
constexpr T large_pair_norm = [] {
    if constexpr (std::same_as<T, float>) {
        return 0x1.c48c5ep+127f;
    } else {
        return 0x1.c48c6001f0abep+1023;
    }
}();

template<class T>
struct norm_boundary_case {
    T finite_input;
    T finite_norm;
    T overflowing_input;
};

template<class T>
constexpr auto norm_boundary = [] {
    if constexpr (std::same_as<T, float>) {
        return norm_boundary_case<T>{0x1.6a09cep+127f, 0x1.ffffdep+127f, 0x1.6a09fep+127f};
    } else {
        return norm_boundary_case<T>{0x1.6a09e667f3bc0p+1023, 0x1.fffffffffffeep+1023,
                                     0x1.6a09e667f3bd9p+1023};
    }
}();

TYPED_TEST(LinalgLevel1ExtremeNumbers, RealGivensScalesBeforeNormalizing) {
    using T = TypeParam;
    const T tiny = std::numeric_limits<T>::denorm_min();
    const T normal = std::numeric_limits<T>::min();
    const auto check = [](T a, T b, T c, T s, T r) {
        const auto rotation = std::linalg::setup_givens_rotation(a, b);
        expect_within_ulps(rotation.c, c, 2);
        expect_within_ulps(rotation.s, s, 2);
        expect_within_ulps(rotation.r, r, 1);
    };
    const auto subnormal = std::linalg::setup_givens_rotation(tiny, tiny);
    expect_within_ulps(subnormal.c, inverse_sqrt_two<T>, 2);
    expect_within_ulps(subnormal.s, inverse_sqrt_two<T>, 2);
    EXPECT_EQ(subnormal.r, tiny);
    check(T{0.75} * normal, normal, static_cast<T>(0.6), static_cast<T>(0.8), T{1.25} * normal);
    check(large_component<T>, large_component<T>,
          inverse_sqrt_two<T>, inverse_sqrt_two<T>, large_pair_norm<T>);
}

TYPED_TEST(LinalgLevel1ExtremeNumbers, RealGivensAtOverflowBoundary) {
    using T = TypeParam;
    const auto [finite_input, finite_norm, overflowing_input] = norm_boundary<T>;
    const auto finite = std::linalg::setup_givens_rotation(finite_input, finite_input);
    expect_within_ulps(finite.r, finite_norm, 1);
    expect_within_ulps(finite.c, inverse_sqrt_two<T>, 2);
    expect_within_ulps(finite.s, inverse_sqrt_two<T>, 2);
    const auto overflow = std::linalg::setup_givens_rotation(overflowing_input, overflowing_input);
    EXPECT_EQ(overflow.r, std::numeric_limits<T>::infinity());
    expect_within_ulps(overflow.c, inverse_sqrt_two<T>, 2);
    expect_within_ulps(overflow.s, inverse_sqrt_two<T>, 2);
}

TYPED_TEST(LinalgLevel1ExtremeNumbers, RealGivensKeepsInfiniteNormAndFiniteOperandCoefficient) {
    using T = TypeParam;
    const T infinity = std::numeric_limits<T>::infinity();
    for (T infinite : {infinity, -infinity}) {
        for (T finite : {T{3}, T{-3}}) {
            const auto first = std::linalg::setup_givens_rotation(infinite, finite);
            EXPECT_EQ(first.r, infinity);
            EXPECT_TRUE(std::isnan(first.c));
            EXPECT_EQ(first.s, T{});
            EXPECT_EQ(std::signbit(first.s), std::signbit(finite));

            const auto second = std::linalg::setup_givens_rotation(finite, infinite);
            EXPECT_EQ(second.r, infinity);
            EXPECT_EQ(second.c, T{});
            EXPECT_EQ(std::signbit(second.c), std::signbit(finite));
            EXPECT_TRUE(std::isnan(second.s));
        }
    }
    const auto both = std::linalg::setup_givens_rotation(infinity, -infinity);
    EXPECT_TRUE(std::isnan(both.c));
    EXPECT_TRUE(std::isnan(both.s));
    EXPECT_TRUE(std::isnan(both.r));
}

TYPED_TEST(LinalgLevel1ExtremeNumbers, RealGivensKeepsSignedZeroAndNanConventions) {
    using T = TypeParam;
    const T zero = -T{};
    const T nan = std::numeric_limits<T>::quiet_NaN();
    const auto check = [](T a, T b, T c, T s, T r) {
        const auto rotation = std::linalg::setup_givens_rotation(a, b);
        EXPECT_EQ(rotation.c, c);
        EXPECT_EQ(rotation.s, s);
        if (std::isnan(r)) {
            EXPECT_TRUE(std::isnan(rotation.r));
        } else {
            EXPECT_EQ(rotation.r, r);
        }
        EXPECT_FALSE(std::signbit(rotation.r) && rotation.r == T{});
    };
    check(zero, zero, T{1}, T{}, T{});
    check(T{-3}, zero, T{-1}, T{}, T{3});
    check(zero, T{-4}, T{}, T{-1}, T{4});
    check(nan, zero, T{1}, T{}, nan);
    check(zero, nan, T{}, T{1}, nan);
    for (const auto rotation : {std::linalg::setup_givens_rotation(nan, T{3}),
                                std::linalg::setup_givens_rotation(T{3}, nan)}) {
        EXPECT_TRUE(std::isnan(rotation.c));
        EXPECT_TRUE(std::isnan(rotation.s));
        EXPECT_TRUE(std::isnan(rotation.r));
    }
}

TYPED_TEST(LinalgLevel1ExtremeNumbers, ComplexGivensNormalizesSubnormalPhase) {
    using T = TypeParam;
    using complex = std::complex<T>;
    const T tiny = std::numeric_limits<T>::denorm_min();
    const T inv_sqrt2 = inverse_sqrt_two<T>;
    const auto check = [=](complex a, complex b, complex s, complex r) {
        const auto rotation = std::linalg::setup_givens_rotation(a, b);
        expect_within_ulps(rotation.c, inv_sqrt2, 2);
        expect_within_ulps(rotation.s.real(), s.real(), 2);
        expect_within_ulps(rotation.s.imag(), s.imag(), 2);
        expect_within_ulps(rotation.c * rotation.c + std::norm(rotation.s), T{1}, 4);
        EXPECT_EQ(rotation.r, r);

        std::mdspan x(&a, std::extents<int, 1>{});
        std::mdspan y(&b, std::extents<int, 1>{});
        std::linalg::apply_givens_rotation(x, y, rotation.c, rotation.s);
        // Each product can round by a fraction of denorm_min before addition.
        expect_within_ulps(a.real(), r.real(), 1);
        expect_within_ulps(a.imag(), r.imag(), 1);
        EXPECT_EQ(b, complex{});
    };
    check({tiny, T{}}, {tiny, T{}}, {inv_sqrt2, T{}}, {tiny, T{}});
    check({tiny, tiny}, {tiny, -tiny}, {T{}, inv_sqrt2}, {tiny, tiny});
}

TEST(LinalgLevel1Givens, ComplexRotationRoundsToMaximumWithNonzeroFirstInput) {
    using complex = std::complex<double>;
    const auto rotation = std::linalg::setup_givens_rotation(
        complex{1.0, 0.0}, complex{1.7976931146382683e308, 2.696539671957402e304});
    expect_within_ulps(rotation.c, 0x0.4p-1022, 1);
    expect_within_ulps(rotation.s.real(), 0x1.ffffff9f5cfd2p-1, 2);
    expect_within_ulps(rotation.s.imag(), -0x1.3a92a2c9f3801p-13, 2);
    EXPECT_EQ(rotation.r.real(), std::numeric_limits<double>::max());
    EXPECT_EQ(rotation.r.imag(), 0.0);
}

TYPED_TEST(LinalgLevel1ExtremeNumbers, ComplexGivensAtOverflowBoundary) {
    using T = TypeParam;
    using complex = std::complex<T>;
    // With a = (3, 1), the exact imaginary component of r is one third of its real part.
    const auto check = [](T finite_input, complex expected, T overflowing_input, T overflow_imag) {
        const complex a{T{3}, T{1}};
        const auto finite = std::linalg::setup_givens_rotation(a, complex{finite_input, finite_input});
        expect_within_ulps(finite.r.real(), expected.real(), 2);
        expect_within_ulps(finite.r.imag(), expected.imag(), 2);
        const auto overflow = std::linalg::setup_givens_rotation(a, complex{overflowing_input, overflowing_input});
        EXPECT_EQ(overflow.r.real(), std::numeric_limits<T>::infinity());
        expect_within_ulps(overflow.r.imag(), overflow_imag, 2);
    };
    if constexpr (std::same_as<T, float>) {
        check(0x1.7d9f32p+127f, {0x1.ffffdcp+127f, 0x1.55553ep+126f},
              0x1.7d9f66p+127f, 0x1.55556cp+126f);
    } else {
        check(0x1.7d9f4cf754628p+1023, {0x1.fffffffffffefp+1023, 0x1.555555555554ap+1022},
              0x1.7d9f4cf754642p+1023, 0x1.5555555555561p+1022);
    }
}

TEST(LinalgLevel1Givens, ComplexRotationInsideOverflowBand) {
    using complex = std::complex<double>;
    constexpr double component = 0x1.6b4b27c8b7836p+1023;
    const auto rotation =
        std::linalg::setup_givens_rotation(complex{12.0, 1.0}, complex{component, component});
    // The exact real component is 0.844 ulp above max(). Infinity and a
    // finite result within a few ulps of the exact value are both allowed.
    if (std::isinf(rotation.r.real())) {
        EXPECT_EQ(rotation.r.real(), std::numeric_limits<double>::infinity());
    } else {
        expect_within_ulps(rotation.r.real(), std::numeric_limits<double>::max(), 2);
    }
    expect_within_ulps(rotation.r.imag(), 0x1.5555555555555p+1020, 2);
}

TYPED_TEST(LinalgLevel1ExtremeNumbers, ComplexGivensRescalesResultComponentsIndependently) {
    using T = TypeParam;
    using complex = std::complex<T>;
    const complex a{large_component<T>, large_component<T>};
    const auto rotation = std::linalg::setup_givens_rotation(a, a);
    expect_within_ulps(rotation.c, inverse_sqrt_two<T>, 2);
    expect_within_ulps(rotation.s.real(), inverse_sqrt_two<T>, 2);
    // Measure the cancellation error at coefficient scale.
    expect_within_ulps(T{1} + rotation.s.imag(), T{1}, 2);
    expect_within_ulps(rotation.r.real(), large_pair_norm<T>, 2);
    expect_within_ulps(rotation.r.imag(), large_pair_norm<T>, 2);
}

TEST(LinalgLevel1Givens, ComplexNonfiniteInputsHaveExplicitConventions) {
    using complex = std::complex<double>;
    const double infinity = std::numeric_limits<double>::infinity();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const auto first = std::linalg::setup_givens_rotation(complex{0.0, infinity}, complex{3.0, 4.0});
    EXPECT_EQ(first.c, 1.0);
    EXPECT_EQ(first.s, complex{});
    EXPECT_EQ(first.r, (complex{0.0, infinity}));

    const auto second = std::linalg::setup_givens_rotation(complex{0.0, 1.0}, complex{-infinity, 3.0});
    EXPECT_EQ(second.c, 0.0);
    EXPECT_EQ(second.s, (complex{0.0, -1.0}));
    EXPECT_EQ(second.r, (complex{0.0, infinity}));

    const auto identity = std::linalg::setup_givens_rotation(complex{nan, 2.0}, complex{});
    EXPECT_EQ(identity.c, 1.0);
    EXPECT_EQ(identity.s, complex{});
    EXPECT_TRUE(std::isnan(identity.r.real()));
    EXPECT_EQ(identity.r.imag(), 2.0);

    for (const auto rotation : {
             std::linalg::setup_givens_rotation(complex{}, complex{infinity, nan}),
             std::linalg::setup_givens_rotation(complex{infinity}, complex{infinity})}) {
        EXPECT_TRUE(std::isnan(rotation.c));
        EXPECT_TRUE(std::isnan(rotation.s.real()));
        EXPECT_TRUE(std::isnan(rotation.s.imag()));
        EXPECT_TRUE(std::isnan(rotation.r.real()));
        EXPECT_TRUE(std::isnan(rotation.r.imag()));
    }
}

TYPED_TEST(LinalgLevel1ExtremeNumbers, NormsScaleComplexComponentsAndIncludeInit) {
    using T = TypeParam;
    using complex = std::complex<T>;
    const T tiny = std::numeric_limits<T>::denorm_min();
    complex data[] = {{tiny, tiny}, {tiny, -tiny}};
    std::mdspan v(data, std::extents<int, 2>{});
    std::mdspan m(data, std::extents<int, 1, 2>{});
    const T expected_subnormal = T{2} * tiny;
    EXPECT_EQ(std::linalg::vector_two_norm(v), expected_subnormal);
    EXPECT_EQ(std::linalg::matrix_frob_norm(m), expected_subnormal);

    data[0] = {large_component<T>, T{}};
    data[1] = complex{};
    const T init = -large_component<T>;
    const T expected_large = large_pair_norm<T>;
    expect_within_ulps(std::linalg::vector_two_norm(v, init), expected_large, 1);
    expect_within_ulps(std::linalg::matrix_frob_norm(m, init), expected_large, 1);
}

TYPED_TEST(LinalgLevel1ExtremeNumbers, NormsAtOverflowBoundary) {
    using T = TypeParam;
    const auto [finite_input, finite_norm, overflowing_input] = norm_boundary<T>;
    T data[]{finite_input, finite_input};
    std::mdspan v(data, std::extents<int, 2>{});
    std::mdspan m(data, std::extents<int, 1, 2>{});
    expect_within_ulps(std::linalg::vector_two_norm(v), finite_norm, 1);
    expect_within_ulps(std::linalg::matrix_frob_norm(m), finite_norm, 1);

    data[0] = overflowing_input;
    data[1] = overflowing_input;
    const T infinity = std::numeric_limits<T>::infinity();
    EXPECT_EQ(std::linalg::vector_two_norm(v), infinity);
    EXPECT_EQ(std::linalg::matrix_frob_norm(m), infinity);
}

TEST(LinalgLevel1Reductions, ScaledSquareSumRenormalizesCompensation) {
    std::linalg::__detail::__linalg_norm_sum<double> sum;
    sum.__add(0x1.fffffffffffffp+0);
    for (int i = 0; i < 128; ++i) {
        sum.__add(0x1p-28);
    }
    // With a fixed leading sum, the final root correction rounds one ulp high.
    EXPECT_EQ(sum.__scaled_norm(), 0x1p+1);
}

TEST(LinalgLevel1Reductions, NormOverflowClassificationWithSmallSquares) {
    std::array<double, 128> data;
    data.fill(0x1p+997);
    std::mdspan v(data.data(), std::extents<int, 128>{});
    std::mdspan m(data.data(), std::extents<int, 1, 128>{});

    constexpr double finite_init = 0x1.fffffffffffcep+1023;
    constexpr double finite_norm = 0x1.fffffffffffeep+1023;
    expect_within_ulps(std::linalg::vector_two_norm(v, finite_init), finite_norm, 1);
    expect_within_ulps(std::linalg::matrix_frob_norm(m, finite_init), finite_norm, 1);

    constexpr double overflow_init = 0x1.ffffffffffff1p+1023;
    constexpr double infinity = std::numeric_limits<double>::infinity();
    EXPECT_EQ(std::linalg::vector_two_norm(v, overflow_init), infinity);
    EXPECT_EQ(std::linalg::matrix_frob_norm(m, overflow_init), infinity);
}

TYPED_TEST(LinalgLevel1ExtremeNumbers, NormsHandleInfinityNanAndZero) {
    using T = TypeParam;
    const T infinity = std::numeric_limits<T>::infinity();
    const T nan = std::numeric_limits<T>::quiet_NaN();
    T data[]{-T{}, T{}};
    std::mdspan v(data, std::extents<int, 2>{});
    std::mdspan m(data, std::extents<int, 1, 2>{});
    const auto check = [&](T init, T expected) {
        for (T result : {std::linalg::vector_two_norm(v, init), std::linalg::matrix_frob_norm(m, init)}) {
            if (std::isnan(expected)) {
                EXPECT_TRUE(std::isnan(result));
            } else {
                EXPECT_EQ(result, expected);
                EXPECT_FALSE(std::signbit(result));
            }
        }
    };
    check(-T{}, T{});
    check(-infinity, infinity);
    data[0] = -infinity;
    data[1] = infinity;
    check(T{}, infinity);
    check(nan, nan);
    data[1] = nan;
    check(T{}, nan);
}
