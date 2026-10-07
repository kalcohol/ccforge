#include <gtest/gtest.h>
#include <linalg>
#include <mdspan>

#include <cmath>
#include <complex>
#include <concepts>
#include <cstddef>
#include <limits>
#include <type_traits>
#include <utility>

namespace {
namespace adl_math {

struct calls {
    int real{};
    int imaginary{};
    int magnitude{};
    int component_magnitude{};
};

struct real_number {
    double value{};
    int* magnitude_calls{};

    constexpr real_number() = default;
    constexpr real_number(double input, int* count = nullptr)
        : value(input), magnitude_calls(count) {}

    constexpr operator double() const { return value; }

    real_number& operator+=(real_number other) {
        value += other.value;
        return *this;
    }

    friend real_number operator+(real_number left, real_number right) {
        left += right;
        return left;
    }

    friend double abs(const real_number& input) {
        if (input.magnitude_calls) ++*input.magnitude_calls;
        return std::abs(input.value);
    }
};

struct complex_number {
    double re{};
    double im{};
    calls* observations{};

    constexpr complex_number() = default;
    constexpr complex_number(double r, double i = 0.0, calls* count = nullptr)
        : re(r), im(i), observations(count) {}

    complex_number& operator+=(const complex_number& other) {
        re += other.re;
        im += other.im;
        return *this;
    }

    friend complex_number operator+(complex_number left, const complex_number& right) {
        left += right;
        return left;
    }

    friend real_number real(const complex_number& input) {
        if (input.observations) ++input.observations->real;
        return {input.re, input.observations
            ? &input.observations->component_magnitude : nullptr};
    }

    friend real_number imag(const complex_number& input) {
        if (input.observations) ++input.observations->imaginary;
        return {input.im, input.observations
            ? &input.observations->component_magnitude : nullptr};
    }

    friend double abs(const complex_number& input) {
        if (input.observations) ++input.observations->magnitude;
        return std::hypot(input.re, input.im);
    }
};

struct implicit_scalar {
    double value{};
    constexpr operator double() const { return value; }
};

struct member_only_real {
    double value{};
    constexpr double real() const { return value; }
    constexpr double imag() const { return 0.0; }

    friend double abs(const member_only_real& input) {
        return std::abs(input.value);
    }
};

struct complex_proxy {
    const complex_number* value;
    int* conversions;

    complex_proxy(const complex_number* input, int* count)
        : value(input), conversions(count) {}
    complex_proxy(const complex_proxy&) = delete;
    complex_proxy(complex_proxy&&) = delete;

    operator complex_number() const {
        ++*conversions;
        return *value;
    }

    friend real_number real(complex_proxy&& input) { return real(*input.value); }
    friend real_number imag(complex_proxy&& input) { return imag(*input.value); }
};

struct complex_accessor {
    using offset_policy = complex_accessor;
    using element_type = const complex_number;
    using reference = complex_proxy;
    using data_handle_type = const complex_number*;

    int* accesses{};
    int* conversions{};

    reference access(data_handle_type pointer, std::size_t index) const {
        ++*accesses;
        return {pointer + index, conversions};
    }

    data_handle_type offset(data_handle_type pointer, std::size_t index) const {
        return pointer + index;
    }
};

struct real_proxy {
    const double* value;
    int* magnitudes;
    int* conversions;

    real_proxy(const double* input, int* count, int* converted)
        : value(input), magnitudes(count), conversions(converted) {}
    real_proxy(const real_proxy&) = delete;
    real_proxy(real_proxy&&) = delete;

    operator double() const {
        ++*conversions;
        return *value;
    }

    friend double abs(real_proxy&& input) {
        ++*input.magnitudes;
        return std::abs(*input.value);
    }
};

struct real_accessor {
    using offset_policy = real_accessor;
    using element_type = const double;
    using reference = real_proxy;
    using data_handle_type = const double*;

    int* accesses{};
    int* magnitudes{};
    int* conversions{};

    reference access(data_handle_type pointer, std::size_t index) const {
        ++*accesses;
        return {pointer + index, magnitudes, conversions};
    }

    data_handle_type offset(data_handle_type pointer, std::size_t index) const {
        return pointer + index;
    }
};

} // namespace adl_math

namespace hetero_math {

struct number {
    double value{};

    number() = default;
    number(double input) : value(input) {}

    number& operator+=(number other) {
        value += other.value;
        return *this;
    }

    friend number operator+(number left, double right) {
        return number{left.value + right};
    }

    friend number operator+(double left, number right) {
        return number{left + right.value};
    }

    friend number operator+(number, number) = delete;

    friend double abs(number input) {
        return std::abs(input.value);
    }
};

} // namespace hetero_math

template<class T>
concept has_same_type_addition = requires(T value) { value + value; };

static_assert(std::semiregular<hetero_math::number>);
static_assert(!has_same_type_addition<hetero_math::number>);
static_assert(std::convertible_to<decltype(
    std::declval<hetero_math::number>() + double{} + double{}),
    hetero_math::number>);
static_assert(std::convertible_to<decltype(
    double{} + std::declval<hetero_math::number>()), hetero_math::number>);

template<class T>
concept has_linalg_absolute_value = requires(T&& value) {
    std::linalg::__detail::__abs_if_needed(std::forward<T>(value));
};

static_assert(std::semiregular<adl_math::complex_number>);
static_assert(std::semiregular<adl_math::complex_accessor>);
static_assert(std::semiregular<adl_math::real_accessor>);
static_assert(std::common_reference_with<adl_math::complex_proxy&&,
                                         const adl_math::complex_number&>);
static_assert(std::common_reference_with<adl_math::real_proxy&&, const double&>);
static_assert(!std::is_default_constructible_v<adl_math::real_proxy>);
static_assert(has_linalg_absolute_value<adl_math::real_number&>);
static_assert(has_linalg_absolute_value<const adl_math::complex_number&>);
static_assert(!has_linalg_absolute_value<adl_math::implicit_scalar>);
static_assert(std::is_same_v<decltype(std::linalg::__detail::__real_if_needed(
    std::declval<adl_math::member_only_real&>())), adl_math::member_only_real&>);
static_assert(std::is_same_v<decltype(std::linalg::__detail::__real_if_needed(
    std::declval<const adl_math::member_only_real&>())), const adl_math::member_only_real&>);
static_assert(std::is_same_v<decltype(std::linalg::__detail::__imag_if_needed(
    std::declval<const adl_math::member_only_real&>())), const adl_math::member_only_real>);


} // namespace

TEST(LinalgAdlHelpers, ComplexAbsoluteSumUsesBothAdlComponents) {
    adl_math::calls observations;
    adl_math::complex_number data[] = {
        {3.0, -4.0, &observations}, {-6.0, 0.0, &observations},
        {-3.0, 4.0, &observations}};
    std::mdspan vector(data, std::extents<int, 3>{});

    EXPECT_DOUBLE_EQ(std::linalg::vector_abs_sum(vector, 2.0), 22.0);
    EXPECT_EQ(observations.real, 3);
    EXPECT_EQ(observations.imaginary, 3);
    EXPECT_EQ(observations.component_magnitude, 6);
    EXPECT_EQ(observations.magnitude, 0);

    static_assert(std::is_same_v<decltype(std::linalg::vector_abs_sum(vector)),
                                 adl_math::complex_number>);
    const auto sum = std::linalg::vector_abs_sum(vector);
    EXPECT_DOUBLE_EQ(sum.re, 20.0);
    EXPECT_DOUBLE_EQ(sum.im, 0.0);
    const auto initialized = std::linalg::vector_abs_sum(
        vector, adl_math::complex_number{1.0, 2.0});
    EXPECT_DOUBLE_EQ(initialized.re, 21.0);
    EXPECT_DOUBLE_EQ(initialized.im, 2.0);
}

TEST(LinalgAdlHelpers, GenericAccumulatorConvertsWholeMagnitudeSum) {
    const hetero_math::number value{-3.0};
    const auto result = std::linalg::__detail::__abs_sum_term_as<
        hetero_math::number, hetero_math::number>(value);
    EXPECT_DOUBLE_EQ(result.value, 3.0);
}

TEST(LinalgAdlHelpers, GenericVectorAccumulatorDoesNotRequireSameTypeAddition) {
    hetero_math::number data[] = {{-3.0}, {4.0}};
    std::mdspan vector(data, std::extents<int, 2>{});
    const auto result = std::linalg::vector_abs_sum(
        vector, hetero_math::number{1.0});
    EXPECT_DOUBLE_EQ(result.value, 8.0);
}

TEST(LinalgAdlHelpers, ComplexMaximumUsesOneNormAndFirstTie) {
    adl_math::complex_number data[] = {{3.0, -4.0}, {-6.0, 0.0}, {-3.0, 4.0}};
    std::mdspan vector(data, std::extents<int, 3>{});
    EXPECT_EQ(std::linalg::vector_idx_abs_max(vector), 0u);
}

TEST(LinalgAdlHelpers, MatrixNormsKeepAdlMagnitudeRatherThanComponentSum) {
    adl_math::calls observations;
    adl_math::complex_number data[] = {
        {3.0, 4.0, &observations}, {6.0, 0.0, &observations},
        {0.0, -2.0, &observations}, {1.0, 0.0, &observations}};
    std::mdspan matrix(data, std::extents<int, 2, 2>{});

    EXPECT_DOUBLE_EQ(std::linalg::matrix_one_norm(matrix), 7.0);
    EXPECT_DOUBLE_EQ(std::linalg::matrix_inf_norm(matrix, 1.0), 12.0);
    EXPECT_EQ(observations.magnitude, 8);
    EXPECT_EQ(observations.real, 0);
    EXPECT_EQ(observations.imaginary, 0);
}

TEST(LinalgAdlHelpers, RealFallbackUsesAdlAbsAndZeroImaginaryPart) {
    adl_math::real_number data[] = {{-3.0}, {4.0}};
    std::mdspan vector(data, std::extents<int, 2>{});
    EXPECT_DOUBLE_EQ(std::linalg::vector_abs_sum(vector, 1.0), 8.0);
    EXPECT_EQ(std::linalg::vector_idx_abs_max(vector), 1u);

    adl_math::member_only_real member{-3.0};
    EXPECT_DOUBLE_EQ(std::linalg::__detail::__real_if_needed(member).value, -3.0);
    EXPECT_DOUBLE_EQ(std::linalg::__detail::__abs_sum_term(member), 3.0);
}

TEST(LinalgAdlHelpers, ProxyAdlQueriesRetainRvalueCategoryWithoutCopyOrConversion) {
    adl_math::calls observations;
    adl_math::complex_number data[] = {
        {3.0, -4.0, &observations}, {-6.0, 0.0, &observations}};
    int accesses = 0;
    int conversions = 0;
    using extents = std::extents<int, 2>;
    std::mdspan<const adl_math::complex_number, extents,
                std::layout_right, adl_math::complex_accessor> vector(
        data, extents{}, adl_math::complex_accessor{&accesses, &conversions});

    EXPECT_DOUBLE_EQ(std::linalg::vector_abs_sum(vector, 0.0), 13.0);
    EXPECT_EQ(accesses, 2);
    EXPECT_EQ(conversions, 0);
    EXPECT_EQ(observations.real, 2);
    EXPECT_EQ(observations.imaginary, 2);
    accesses = 0;
    EXPECT_EQ(std::linalg::vector_idx_abs_max(vector), 0u);
    EXPECT_EQ(accesses, 2);
    EXPECT_EQ(conversions, 0);
}

TEST(LinalgAdlHelpers, NativeComplexAndIntegralMagnitudeControlsStayIntact) {
    using complex = std::complex<float>;
    complex data[] = {{0x1p24f, 1.0f}};
    std::mdspan vector(data, std::extents<int, 1>{});
    EXPECT_DOUBLE_EQ(std::linalg::vector_abs_sum(vector, 0.0), 0x1p24 + 1.0);

    const int minimum = std::numeric_limits<int>::min();
    EXPECT_EQ(std::linalg::__detail::__abs_if_needed(minimum),
              static_cast<unsigned>(std::numeric_limits<int>::max()) + 1u);
    EXPECT_EQ(std::linalg::__detail::__abs_if_needed(7u), 7u);
}

TEST(LinalgAdlHelpers, ArithmeticValueTypeDoesNotRequireDefaultConstructibleProxy) {
    const double data[] = {-3.0, 4.0};
    int accesses = 0;
    int magnitudes = 0;
    int conversions = 0;
    using extents = std::extents<int, 2>;
    std::mdspan<const double, extents, std::layout_right, adl_math::real_accessor> vector(
        data, extents{}, adl_math::real_accessor{&accesses, &magnitudes, &conversions});

    EXPECT_DOUBLE_EQ(std::linalg::vector_abs_sum(vector, 1.0), 8.0);
    EXPECT_EQ(accesses, 2);
    EXPECT_EQ(magnitudes, 2);
    EXPECT_EQ(conversions, 0);
    accesses = 0;
    magnitudes = 0;
    EXPECT_EQ(std::linalg::vector_idx_abs_max(vector), 1u);
    EXPECT_EQ(accesses, 2);
    EXPECT_EQ(magnitudes, 2);
    EXPECT_EQ(conversions, 0);
}
