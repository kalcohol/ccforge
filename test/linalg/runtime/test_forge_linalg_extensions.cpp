#include <forge/linalg.hpp>
#include <gtest/gtest.h>

#include <cmath>
#include <complex>
#include <concepts>
#include <cstdint>
#include <limits>
#include <mdspan>
#include <type_traits>

namespace linalg_givens_test {

struct real {
    double value{};

    constexpr real() = default;
    constexpr real(double v) : value(v) {}
    constexpr operator double() const { return value; }
};

} // namespace linalg_givens_test

// Use an explicit specialization for the program-defined real type.
template<>
class std::complex<linalg_givens_test::real> {
public:
    using value_type = linalg_givens_test::real;

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

namespace linalg_givens_test {

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

real abs(real x) { return std::abs(x.value); }
rvalue_magnitude abs(const complex& z) { return {std::hypot(z.real().value, z.imag().value)}; }
double norm(const complex& z) { return z.real() * z.real() + z.imag() * z.imag(); }
complex conj(const complex& z) { return complex{z.real(), -z.imag()}; }

template<class Magnitude>
concept lvalue_magnitude_product = requires(Magnitude& value) { value * value; };

static_assert(std::same_as<decltype(abs(complex{}) * abs(complex{})), double>);
static_assert(!lvalue_magnitude_product<decltype(abs(complex{}))>);

} // namespace linalg_givens_test

namespace {

template<class T>
concept forge_givens_setup = requires(T coefficient) {
    forge::setup_givens_rotation(coefficient, coefficient);
};

template<class View, class T>
concept forge_givens_application = requires(View x, T coefficient) {
    forge::apply_givens_rotation(x, x, coefficient, coefficient);
};

template<class T>
concept explicit_forge_givens_setup = requires(T coefficient) {
    forge::setup_givens_rotation<T>(coefficient, coefficient);
};

template<class View, class T>
concept explicit_forge_givens_application = requires {
    static_cast<void (*)(View, View, T, T)>(
        &forge::apply_givens_rotation<typename View::extents_type, typename View::layout_type,
                                      typename View::accessor_type, typename View::extents_type,
                                      typename View::layout_type, typename View::accessor_type, T>);
};

template<class T>
concept standard_givens_setup = requires(T value) {
    std::linalg::setup_givens_rotation(value, value);
};

using integer_vector = std::mdspan<int, std::extents<int, 1>>;
static_assert(forge_givens_setup<int> && !forge_givens_setup<unsigned>);
static_assert(forge_givens_application<integer_vector, int> &&
              !forge_givens_application<integer_vector, unsigned>);
static_assert(explicit_forge_givens_setup<int&> && !explicit_forge_givens_setup<unsigned&>);
static_assert(explicit_forge_givens_application<integer_vector, int&> &&
              !explicit_forge_givens_application<integer_vector, unsigned&>);
static_assert(forge_givens_setup<linalg_givens_test::complex> &&
              !standard_givens_setup<linalg_givens_test::complex>);

struct additive_element {
    int value;

    friend additive_element operator+(additive_element a, additive_element b) {
        return {a.value + b.value};
    }

    friend additive_element operator*(int coefficient, additive_element x) {
        return {coefficient * x.value};
    }
};

struct magnitude_element {
    double value;
    explicit operator double() const noexcept { return value; }
};

template<class T>
struct magnitude_accessor {
    using element_type = const T;
    using data_handle_type = const T*;
    using offset_policy = magnitude_accessor;

    struct reference {
        data_handle_type element;

        operator T() const noexcept { return *element; }
        friend double abs(reference value) noexcept {
            return std::abs(static_cast<double>(*value.element));
        }
    };

    reference access(data_handle_type p, std::size_t i) const noexcept {
        return {p + i};
    }
    data_handle_type offset(data_handle_type p, std::size_t i) const noexcept { return p + i; }
};

} // namespace

TEST(ForgeLinalgExtensions, IntegerNormsAndSumOfSquares) {
    int data[]{3, 4};
    std::mdspan vector(data, std::extents<int, 2>{});
    std::mdspan matrix(data, std::extents<int, 1, 2>{});

    EXPECT_EQ(forge::vector_two_norm(vector), 5);
    EXPECT_EQ(forge::matrix_frob_norm(matrix), 5);
    EXPECT_DOUBLE_EQ(forge::matrix_frob_norm(matrix, 0.0), 5.0);

    auto ssq = forge::vector_sum_of_squares(vector, forge::sum_of_squares_result<int>{0, 1});
    EXPECT_EQ(ssq.scaling_factor, 1);
    EXPECT_EQ(ssq.scaled_sum_of_squares, 25);

    data[0] = 128;
    EXPECT_EQ(forge::vector_two_norm(vector, std::int8_t{}),
              std::numeric_limits<std::int8_t>::max());
    EXPECT_EQ(forge::matrix_frob_norm(matrix, std::int8_t{}),
              std::numeric_limits<std::int8_t>::max());
}

TEST(ForgeLinalgExtensions, FloatingSumOfSquares) {
    const double data[]{3.0, 4.0};
    std::mdspan vector(data, std::extents<int, 2>{});
    const auto ssq = forge::vector_sum_of_squares(
        vector, forge::sum_of_squares_result<double>{0.0, 1.0});

    EXPECT_DOUBLE_EQ(ssq.scaling_factor, 4.0);
    EXPECT_DOUBLE_EQ(ssq.scaled_sum_of_squares, 1.5625);
}

TEST(ForgeLinalgExtensions, IntegerNormsDeduceResultFromProxyMagnitude) {
    using accessor = magnitude_accessor<int>;
    using vector = std::mdspan<const int, std::extents<int, 2>, std::layout_right, accessor>;
    using matrix = std::mdspan<const int, std::extents<int, 1, 2>, std::layout_right, accessor>;
    const int data[]{1, -1};
    const vector v(data, vector::mapping_type{}, accessor{});
    const matrix m(data, matrix::mapping_type{}, accessor{});

    static_assert(std::is_same_v<decltype(forge::vector_two_norm(v)), double>);
    static_assert(std::is_same_v<decltype(forge::matrix_frob_norm(m)), double>);
    EXPECT_DOUBLE_EQ(forge::vector_two_norm(v), std::sqrt(2.0));
    EXPECT_DOUBLE_EQ(forge::matrix_frob_norm(m), std::sqrt(2.0));
}

TEST(ForgeLinalgExtensions, SumOfSquaresUsesProxyMagnitude) {
    using accessor = magnitude_accessor<magnitude_element>;
    using vector =
        std::mdspan<const magnitude_element, std::extents<int, 2>, std::layout_right, accessor>;
    const magnitude_element data[]{{3.0}, {-4.0}};
    const vector v(data, vector::mapping_type{}, accessor{});

    const auto integral = forge::vector_sum_of_squares(v, forge::sum_of_squares_result<int>{0, 1});
    EXPECT_EQ(integral.scaling_factor, 1);
    EXPECT_EQ(integral.scaled_sum_of_squares, 25);
    const auto floating =
        forge::vector_sum_of_squares(v, forge::sum_of_squares_result<double>{0.0, 1.0});
    EXPECT_DOUBLE_EQ(floating.scaling_factor, 4.0);
    EXPECT_DOUBLE_EQ(floating.scaled_sum_of_squares, 1.5625);
}

TEST(ForgeLinalgExtensions, MixedNormAndSumOfSquaresTypes) {
    const int integer_data[]{3, 4};
    const double floating_data[]{3.0, 4.0};
    std::mdspan integer_vector(integer_data, std::extents<int, 2>{});
    std::mdspan floating_vector(floating_data, std::extents<int, 2>{});
    std::mdspan floating_matrix(floating_data, std::extents<int, 1, 2>{});

    const auto floating_ssq = forge::vector_sum_of_squares(
        integer_vector, forge::sum_of_squares_result<double>{0.0, 1.0});
    EXPECT_DOUBLE_EQ(floating_ssq.scaling_factor, 4.0);
    EXPECT_DOUBLE_EQ(floating_ssq.scaled_sum_of_squares, 1.5625);

    const auto integer_ssq = forge::vector_sum_of_squares(
        floating_vector, forge::sum_of_squares_result<int>{0, 1});
    EXPECT_EQ(integer_ssq.scaling_factor, 1);
    EXPECT_EQ(integer_ssq.scaled_sum_of_squares, 25);

    EXPECT_EQ(forge::vector_two_norm(floating_vector, 0), 5);
    EXPECT_EQ(forge::matrix_frob_norm(floating_matrix, 0), 5);
}

TEST(LinalgNorms, InitializedNormsAcceptRvalueMagnitudeProducts) {
    using linalg_givens_test::complex;
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

TEST(ForgeLinalgExtensions, IntegerGivensWithMixedVectorTypes) {
    static_assert(noexcept(forge::setup_givens_rotation(3, 0)));
    static_assert(std::is_same_v<decltype(forge::setup_givens_rotation(3, 0)),
                                 std::linalg::setup_givens_rotation_result<int>>);
    const auto rotation = forge::setup_givens_rotation(3, 0);
    EXPECT_EQ(rotation.r, 3);
    EXPECT_EQ(rotation.c, 1);
    EXPECT_EQ(rotation.s, 0);

    int first[]{3, 4};
    double second[]{-1.0, -2.0};
    std::mdspan x(first, std::extents<int, 2>{});
    std::mdspan y(second, std::extents<int, 2>{});
    forge::apply_givens_rotation(x, y, rotation.c, rotation.s);
    EXPECT_EQ(first[0], 3);
    EXPECT_EQ(first[1], 4);
    EXPECT_DOUBLE_EQ(second[0], -1.0);
    EXPECT_DOUBLE_EQ(second[1], -2.0);
}

TEST(ForgeLinalgExtensions, SignedGivensAcceptsAdditiveElements) {
    additive_element first[]{{3}};
    additive_element second[]{{4}};
    std::mdspan x(first, std::extents<int, 1>{});
    std::mdspan y(second, std::extents<int, 1>{});

    forge::apply_givens_rotation(x, y, 0, -1);
    EXPECT_EQ(first[0].value, -4);
    EXPECT_EQ(second[0].value, 3);
}

TEST(ForgeLinalgExtensions, ComplexGivensAcceptsProgramDefinedReal) {
    using linalg_givens_test::complex;
    const auto rotation = forge::setup_givens_rotation(complex{3.0}, complex{0.0, 4.0});

    static_assert(std::is_same_v<decltype(rotation),
                                 const std::linalg::setup_givens_rotation_result<complex>>);
    EXPECT_DOUBLE_EQ(rotation.c.value, 0.6);
    EXPECT_DOUBLE_EQ(rotation.s.real().value, 0.0);
    EXPECT_DOUBLE_EQ(rotation.s.imag().value, -0.8);
    EXPECT_DOUBLE_EQ(rotation.r.real().value, 5.0);
    EXPECT_DOUBLE_EQ(rotation.r.imag().value, 0.0);
}

TEST(ForgeLinalgExtensions, CustomRealComplexCompoundArithmetic) {
    using linalg_givens_test::complex;
    using linalg_givens_test::real;
    complex scaled{2.0, 3.0};
    const real& re = reinterpret_cast<real (&)[2]>(scaled)[0];
    scaled *= re;
    EXPECT_DOUBLE_EQ(scaled.real().value, 4.0);
    EXPECT_DOUBLE_EQ(scaled.imag().value, 6.0);
    scaled /= re;
    EXPECT_DOUBLE_EQ(scaled.real().value, 1.0);
    EXPECT_DOUBLE_EQ(scaled.imag().value, 1.5);

    constexpr long double delta = std::numeric_limits<long double>::epsilon();
    complex difference{1.0, 1.0};
    difference -= std::complex<long double>{1.0L + delta, 1.0L - delta};
    EXPECT_DOUBLE_EQ(difference.real().value, -static_cast<double>(delta));
    EXPECT_DOUBLE_EQ(difference.imag().value, static_cast<double>(delta));
}
