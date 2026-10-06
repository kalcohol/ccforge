#include "simd_test_common.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <concepts>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace {

using namespace simd_test;

template<class A, class B, class V>
constexpr bool binary_math_returns_vector() {
#define CHECK_MATH_RESULT(name, result) \
    { std::simd::name(std::declval<A>(), std::declval<B>()) } -> std::same_as<result>;
    return requires {
        CHECK_MATH_RESULT(fmod, V)
        CHECK_MATH_RESULT(remainder, V)
        CHECK_MATH_RESULT(copysign, V)
        CHECK_MATH_RESULT(nextafter, V)
        CHECK_MATH_RESULT(fdim, V)
        CHECK_MATH_RESULT(fmax, V)
        CHECK_MATH_RESULT(fmin, V)
        CHECK_MATH_RESULT(atan2, V)
        CHECK_MATH_RESULT(pow, V)
        CHECK_MATH_RESULT(hypot, V)
        CHECK_MATH_RESULT(isgreater, typename V::mask_type)
        CHECK_MATH_RESULT(isgreaterequal, typename V::mask_type)
        CHECK_MATH_RESULT(isless, typename V::mask_type)
        CHECK_MATH_RESULT(islessequal, typename V::mask_type)
        CHECK_MATH_RESULT(islessgreater, typename V::mask_type)
        CHECK_MATH_RESULT(isunordered, typename V::mask_type)
        { std::simd::remquo(std::declval<A>(), std::declval<B>(),
              std::declval<std::simd::rebind_t<int, V>*>()) } -> std::same_as<V>;
    };
#undef CHECK_MATH_RESULT
}

template<class A, class B>
constexpr bool binary_math_rejects_operands() {
#define REJECT_MATH_OPERANDS(name) \
    !requires { std::simd::name(std::declval<A>(), std::declval<B>()); }
    return REJECT_MATH_OPERANDS(fmod) && REJECT_MATH_OPERANDS(remainder) &&
        REJECT_MATH_OPERANDS(copysign) && REJECT_MATH_OPERANDS(nextafter) &&
        REJECT_MATH_OPERANDS(fdim) && REJECT_MATH_OPERANDS(fmax) &&
        REJECT_MATH_OPERANDS(fmin) && REJECT_MATH_OPERANDS(atan2) &&
        REJECT_MATH_OPERANDS(pow) && REJECT_MATH_OPERANDS(hypot) &&
        REJECT_MATH_OPERANDS(isgreater) && REJECT_MATH_OPERANDS(isgreaterequal) &&
        REJECT_MATH_OPERANDS(isless) && REJECT_MATH_OPERANDS(islessequal) &&
        REJECT_MATH_OPERANDS(islessgreater) && REJECT_MATH_OPERANDS(isunordered) &&
        !requires { std::simd::remquo(std::declval<A>(), std::declval<B>(),
            std::declval<std::simd::rebind_t<int, float4>*>()); };
#undef REJECT_MATH_OPERANDS
}

template<class A, class B, class C, class V>
constexpr bool ternary_math_returns_vector() {
    return requires {
        { std::simd::fma(std::declval<A>(), std::declval<B>(), std::declval<C>()) } -> std::same_as<V>;
        { std::simd::hypot(std::declval<A>(), std::declval<B>(), std::declval<C>()) } -> std::same_as<V>;
        { std::simd::lerp(std::declval<A>(), std::declval<B>(), std::declval<C>()) } -> std::same_as<V>;
    };
}

template<class A, class B, class C>
constexpr bool ternary_math_rejects_operands() {
    return !requires { std::simd::fma(std::declval<A>(), std::declval<B>(), std::declval<C>()); } &&
        !requires { std::simd::hypot(std::declval<A>(), std::declval<B>(), std::declval<C>()); } &&
        !requires { std::simd::lerp(std::declval<A>(), std::declval<B>(), std::declval<C>()); };
}

struct throwing_math_vector {
    operator double4() const {
        throw std::runtime_error("math operand conversion");
    }
};

struct explicit_math_vector {
    explicit operator double4() const;
};

struct counted_math_vector {
    double4 lanes;
    int* conversions;

    operator double4() const& noexcept {
        ++*conversions;
        return lanes;
    }
};

struct const_lvalue_math_vector {
    double4 lanes;
    int* conversions;
    int* additions;

    operator double4() const& noexcept {
        ++*conversions;
        return lanes;
    }

    friend double4 operator+(const const_lvalue_math_vector& left,
                             const const_lvalue_math_vector& right) noexcept {
        ++*left.additions;
        return left.lanes + right.lanes;
    }
    friend double4 operator+(const_lvalue_math_vector&, const_lvalue_math_vector&) = delete;
    friend double4 operator+(const const_lvalue_math_vector&&, const const_lvalue_math_vector&&) = delete;
};

struct mutable_lvalue_math_vector {
    double4 lanes;
    operator double4() const& noexcept { return lanes; }
    friend double4 operator+(mutable_lvalue_math_vector& left, mutable_lvalue_math_vector& right) noexcept {
        return left.lanes + right.lanes;
    }
};

struct rvalue_math_vector {
    double4 lanes;
    operator double4() const& noexcept { return lanes; }
    friend double4 operator+(rvalue_math_vector&& left, rvalue_math_vector&& right) noexcept {
        return left.lanes + right.lanes;
    }
};

struct invalid_math_operand {};
struct invalid_math_abi {};
using invalid_math_vector = std::simd::basic_vec<double, invalid_math_abi>;
using bool_math_vector = std::simd::basic_vec<bool, typename int4::abi_type>;

static_assert(std::is_convertible_v<const int4&, double4>);
static_assert(std::is_convertible_v<int, double4>);
static_assert(!std::is_convertible_v<double, float4>);
static_assert(binary_math_returns_vector<double4, int, double4>());
static_assert(binary_math_returns_vector<int, double4, double4>());
static_assert(binary_math_returns_vector<const double4&, const int4&, double4>());
static_assert(binary_math_returns_vector<int4&&, double4&, double4>());
static_assert(binary_math_returns_vector<float4&, const double4&, double4>());
static_assert(binary_math_returns_vector<const double4&, float4&&, double4>());
static_assert(binary_math_returns_vector<float4, float, float4>());
static_assert(binary_math_returns_vector<double4, bool, double4>());
static_assert(binary_math_returns_vector<bool, double4, double4>());
static_assert(binary_math_returns_vector<double4, throwing_math_vector, double4>());
static_assert(binary_math_rejects_operands<float4, double>());
static_assert(binary_math_rejects_operands<const double&, float4&>());
static_assert(binary_math_rejects_operands<double4, int3>());
static_assert(binary_math_rejects_operands<int3, double4>());
static_assert(binary_math_rejects_operands<int4, int>());
static_assert(binary_math_rejects_operands<double, int>());
static_assert(binary_math_rejects_operands<bool, bool>());
static_assert(binary_math_rejects_operands<double4, mask4>());
static_assert(binary_math_rejects_operands<mask4, double4>());
static_assert(binary_math_rejects_operands<double4, invalid_math_vector>());
static_assert(binary_math_rejects_operands<invalid_math_vector, double4>());
static_assert(binary_math_rejects_operands<double4, bool_math_vector>());
static_assert(binary_math_rejects_operands<double4, invalid_math_operand>());
static_assert(binary_math_rejects_operands<double4, explicit_math_vector>());
static_assert(binary_math_rejects_operands<volatile double4&, int>());

static_assert(binary_math_returns_vector<const const_lvalue_math_vector&, int, double4>());
static_assert(binary_math_returns_vector<int, const_lvalue_math_vector&, double4>());
static_assert(binary_math_returns_vector<const_lvalue_math_vector&&, int, double4>());
static_assert(binary_math_returns_vector<const const_lvalue_math_vector&,
    const const_lvalue_math_vector&, double4>());
static_assert(requires(mutable_lvalue_math_vector& value) { value + value; });
static_assert(requires(rvalue_math_vector& value) { std::move(value) + std::move(value); });
static_assert(std::is_convertible_v<const mutable_lvalue_math_vector&, double4>);
static_assert(std::is_convertible_v<const rvalue_math_vector&, double4>);
static_assert(binary_math_rejects_operands<mutable_lvalue_math_vector&, int>());
static_assert(binary_math_rejects_operands<int, mutable_lvalue_math_vector&>());
static_assert(binary_math_rejects_operands<rvalue_math_vector&&, int>());
static_assert(binary_math_rejects_operands<int, rvalue_math_vector&&>());
static_assert(binary_math_returns_vector<mutable_lvalue_math_vector&, double4, double4>());
static_assert(binary_math_returns_vector<double4, rvalue_math_vector&&, double4>());

// Exercise all seven deduction patterns, with no scalar-driven promotion.
static_assert(ternary_math_returns_vector<double4, double4, double4, double4>());
static_assert(ternary_math_returns_vector<int, double4, double4, double4>());
static_assert(ternary_math_returns_vector<double4, int, double4, double4>());
static_assert(ternary_math_returns_vector<double4, double4, int, double4>());
static_assert(ternary_math_returns_vector<int, int, double4, double4>());
static_assert(ternary_math_returns_vector<int, double4, int, double4>());
static_assert(ternary_math_returns_vector<double4, int, int, double4>());
static_assert(ternary_math_returns_vector<const int4&, double4&, float4&&, double4>());
static_assert(ternary_math_returns_vector<float4, double4, int4, double4>());
static_assert(ternary_math_returns_vector<float4, int4, double4, double4>());
static_assert(ternary_math_returns_vector<bool, double4, bool, double4>());
static_assert(ternary_math_rejects_operands<float4, double, float>());
static_assert(ternary_math_rejects_operands<double, float4, float>());
static_assert(ternary_math_rejects_operands<float, double, float4>());
static_assert(ternary_math_rejects_operands<float4, float4, double>());
static_assert(ternary_math_rejects_operands<float4, int4, float4>());
static_assert(ternary_math_rejects_operands<double4, int3, int>());
static_assert(ternary_math_rejects_operands<double4, mask4, int>());
static_assert(ternary_math_rejects_operands<double4, invalid_math_vector, int>());
static_assert(ternary_math_rejects_operands<bool, bool, bool>());
static_assert(ternary_math_rejects_operands<int4, int, int>());

static_assert(ternary_math_returns_vector<const const_lvalue_math_vector&, int, int, double4>());
static_assert(ternary_math_returns_vector<int, const_lvalue_math_vector&, int, double4>());
static_assert(ternary_math_returns_vector<int, int, const_lvalue_math_vector&&, double4>());
static_assert(ternary_math_rejects_operands<mutable_lvalue_math_vector&, int, int>());
static_assert(ternary_math_rejects_operands<int, mutable_lvalue_math_vector&, int>());
static_assert(ternary_math_rejects_operands<int, int, mutable_lvalue_math_vector&>());
static_assert(ternary_math_rejects_operands<rvalue_math_vector&&, int, int>());
static_assert(ternary_math_rejects_operands<int, rvalue_math_vector&&, int>());
static_assert(ternary_math_rejects_operands<int, int, rvalue_math_vector&&>());

template<class V, class A, class B>
concept explicit_binary_math = requires {
    std::simd::pow<V>(std::declval<A>(), std::declval<B>());
    std::simd::fmax<V>(std::declval<A>(), std::declval<B>());
};
static_assert(explicit_binary_math<double4, float4, double4>);
static_assert(!explicit_binary_math<float4, float4, double4>);
static_assert(!explicit_binary_math<float4, float4, double>);

static_assert(noexcept(std::simd::lerp(std::declval<const double4&>(),
    std::declval<const double4&>(), std::declval<const double4&>())));
static_assert(!noexcept(std::simd::lerp(std::declval<const double4&>(), 2, 0.5)));
static_assert(!noexcept(std::simd::lerp(std::declval<const double4&>(),
    std::declval<throwing_math_vector>(), 0.5)));
static_assert(!noexcept(std::simd::pow(std::declval<const double4&>(), 2)));

// Special math keeps its independent promotion policy in this patch.
static_assert(std::is_same_v<decltype(std::simd::beta(std::declval<float4>(), 2.0)),
    std::simd::rebind_t<double, float4>>);
static_assert(std::is_same_v<decltype(std::simd::ellint_3(std::declval<float4>(), 0.25, 0.5f)),
    std::simd::rebind_t<double, float4>>);

constexpr bool constexpr_mixed_math_operands() {
    const double4 values([](auto i) { return static_cast<double>(decltype(i)::value + 1); });
    const int4 limits([](auto i) { return static_cast<int>(decltype(i)::value + 2); });
    const auto maxima = std::simd::fmax(values, limits);
    const auto minima = std::simd::fmin(2, values);
    const auto signs = std::simd::copysign(values, -1);
    const auto interpolated = std::simd::lerp(values, 5, 0.5);
    return maxima[0] == 2.0 && maxima[3] == 5.0 &&
        minima[0] == 1.0 && minima[3] == 2.0 &&
        signs[0] == -1.0 && signs[3] == -4.0 &&
        interpolated[0] == 3.0 && interpolated[3] == 4.5;
}
static_assert(constexpr_mixed_math_operands());

TEST(SimdMathExt, MixedBinaryOperandsKeepTheDeducedVector) {
    const double4 values = load_vec<double4>(std::array<double, 4>{{2.5, 3.5, 4.5, 5.5}});
    const int4 integers = load_vec<int4>(std::array<int, 4>{{2, 2, 3, 3}});
    const float4 floats = load_vec<float4>(std::array<float, 4>{{1.0f, 2.0f, 3.0f, 4.0f}});
#define CHECK_MIXED_BINARY(name) \
    { \
        const auto scalar_right = std::simd::name(values, 2); \
        const auto scalar_left = std::simd::name(2, values); \
        const auto vector_right = std::simd::name(values, integers); \
        const auto vector_left = std::simd::name(integers, values); \
        for (std::simd::simd_size_type i = 0; i < double4::size; ++i) { \
            EXPECT_DOUBLE_EQ(scalar_right[i], std::name(values[i], 2.0)); \
            EXPECT_DOUBLE_EQ(scalar_left[i], std::name(2.0, values[i])); \
            EXPECT_DOUBLE_EQ(vector_right[i], std::name(values[i], static_cast<double>(integers[i]))); \
            EXPECT_DOUBLE_EQ(vector_left[i], std::name(static_cast<double>(integers[i]), values[i])); \
        } \
    }
    CHECK_MIXED_BINARY(fmod)
    CHECK_MIXED_BINARY(remainder)
    CHECK_MIXED_BINARY(copysign)
    CHECK_MIXED_BINARY(nextafter)
    CHECK_MIXED_BINARY(fdim)
    CHECK_MIXED_BINARY(fmax)
    CHECK_MIXED_BINARY(fmin)
    CHECK_MIXED_BINARY(atan2)
    CHECK_MIXED_BINARY(pow)
    CHECK_MIXED_BINARY(hypot)
#undef CHECK_MIXED_BINARY
    const auto mixed_vectors = std::simd::pow(floats, values);
    const auto compared = std::simd::isgreater(values, integers);
    const auto bool_operand = std::simd::fmax(false, values);
    std::simd::rebind_t<int, double4> quotients;
    const auto remainders = std::simd::remquo(values, 2, &quotients);
    for (std::simd::simd_size_type i = 0; i < double4::size; ++i) {
        EXPECT_DOUBLE_EQ(mixed_vectors[i], std::pow(static_cast<double>(floats[i]), values[i]));
        EXPECT_EQ(compared[i], values[i] > static_cast<double>(integers[i]));
        EXPECT_DOUBLE_EQ(bool_operand[i], values[i]);
        int quotient = 0;
        EXPECT_DOUBLE_EQ(remainders[i], std::remquo(values[i], 2.0, &quotient));
        EXPECT_EQ(quotients[i], quotient);
    }
}

TEST(SimdMathExt, MixedTernaryOperandsNormalizeEveryPosition) {
    const double4 values = load_vec<double4>(std::array<double, 4>{{2.5, 3.5, 4.5, 5.5}});
    const double4 twos(2);
    const double4 halves(0.5);
#define CHECK_MIXED_TERNARY(name) \
    { \
        const std::array<double4, 7> results{{ \
            std::simd::name(values, twos, halves), \
            std::simd::name(2, values, halves), \
            std::simd::name(values, 2, halves), \
            std::simd::name(values, halves, 2), \
            std::simd::name(2, 1, values), \
            std::simd::name(2, values, 1), \
            std::simd::name(values, 2, 1) }}; \
        for (std::simd::simd_size_type i = 0; i < double4::size; ++i) { \
            const std::array<double, 7> expected{{ \
                std::name(values[i], 2.0, 0.5), std::name(2.0, values[i], 0.5), \
                std::name(values[i], 2.0, 0.5), std::name(values[i], 0.5, 2.0), \
                std::name(2.0, 1.0, values[i]), std::name(2.0, values[i], 1.0), \
                std::name(values[i], 2.0, 1.0) }}; \
            for (std::size_t j = 0; j < results.size(); ++j) { \
                EXPECT_DOUBLE_EQ(results[j][i], expected[j]); \
            } \
        } \
    }
    CHECK_MIXED_TERNARY(fma)
    CHECK_MIXED_TERNARY(hypot)
    CHECK_MIXED_TERNARY(lerp)
#undef CHECK_MIXED_TERNARY
    const int4 integers(2);
    const auto mixed_vectors = std::simd::fma(integers, values, 1);
    for (std::simd::simd_size_type i = 0; i < double4::size; ++i) {
        EXPECT_DOUBLE_EQ(mixed_vectors[i], std::fma(2.0, values[i], 1.0));
    }
}

TEST(SimdMathExt, ThrowingImplicitOperandConversionPropagates) {
    const double4 values(2);
    const throwing_math_vector operand;
    EXPECT_THROW(std::simd::pow(values, operand), std::runtime_error);
    EXPECT_THROW(std::simd::fmax(operand, values), std::runtime_error);
    EXPECT_THROW(std::simd::fma(values, operand, 1), std::runtime_error);
    EXPECT_THROW(std::simd::hypot(operand, values, 1), std::runtime_error);
    EXPECT_THROW(std::simd::lerp(values, operand, 0.5), std::runtime_error);
}

TEST(SimdMathExt, SuccessfulImplicitOperandConversionOccursOnce) {
    const double4 values(2);
    int conversions = 0;
    const counted_math_vector operand{double4(3), &conversions};
    const auto check_once = [&conversions](auto call, auto expected) {
        conversions = 0;
        const auto result = call();
        EXPECT_EQ(conversions, 1);
        for (std::simd::simd_size_type i = 0; i < double4::size; ++i) {
            if constexpr (std::is_same_v<decltype(expected), bool>) {
                EXPECT_EQ(result[i], expected);
            } else {
                EXPECT_DOUBLE_EQ(result[i], expected);
            }
        }
    };

    check_once([&] { return std::simd::fmax(values, operand); }, 3.0);
    check_once([&] { return std::simd::pow(operand, values); }, 9.0);
    check_once([&] { return std::simd::isgreater(operand, values); }, true);
    std::simd::rebind_t<int, double4> quotients;
    check_once([&] { return std::simd::remquo(values, operand, &quotients); }, -1.0);
    check_once([&] { return std::simd::fma(values, operand, 1); }, 7.0);
    check_once([&] { return std::simd::fma(operand, values, 1); }, 7.0);
    check_once([&] { return std::simd::fma(values, 1, operand); }, 5.0);

    int other_conversions = 0;
    const counted_math_vector other{double4(0.5), &other_conversions};
    check_once([&] { return std::simd::fma(operand, values, other); }, 6.5);
    EXPECT_EQ(other_conversions, 1);
    other_conversions = 0;
    check_once([&] { return std::simd::hypot(operand, other, values); }, std::hypot(3.0, 0.5, 2.0));
    EXPECT_EQ(other_conversions, 1);
    other_conversions = 0;
    check_once([&] { return std::simd::lerp(values, operand, other); }, 2.5);
    EXPECT_EQ(other_conversions, 1);
}

TEST(SimdMathExt, CustomPrimaryVectorIsDeducedFromConstLvalueAddition) {
    int conversions = 0;
    int additions = 0;
    const const_lvalue_math_vector value{double4(2), &conversions, &additions};
    const auto maxima = std::simd::fmax(value, 3);
    EXPECT_EQ(conversions, 1);
    EXPECT_EQ(additions, 0);
    conversions = 0;
    const auto fused = std::simd::fma(2, value, 1);
    EXPECT_EQ(conversions, 1);
    EXPECT_EQ(additions, 0);
    conversions = 0;
    const auto interpolated = std::simd::lerp(value, value, value);
    EXPECT_EQ(conversions, 3);
    EXPECT_EQ(additions, 0);
    for (std::simd::simd_size_type i = 0; i < double4::size; ++i) {
        EXPECT_EQ(maxima[i], 3.0);
        EXPECT_EQ(fused[i], 5.0);
        EXPECT_EQ(interpolated[i], 2.0);
    }
}

// EXPECT_NEAR is meaningless when a lane legitimately produces a non-finite value
// (e.g. exp2(1000) overflows float to +inf, tgamma(0) is the gamma pole): for two
// equal infinities |a-b| is NaN and NaN <= tol is false, so the assertion spuriously
// fails even though the simd result matches the scalar reference exactly. Compare
// non-finite lanes by classification and fall back to a tolerance for finite ones.
::testing::AssertionResult lane_near(float got, float ref, float tol) {
    if (std::isnan(ref)) {
        return std::isnan(got) ? ::testing::AssertionSuccess()
                               : ::testing::AssertionFailure() << "expected NaN, got " << got;
    }
    if (std::isinf(ref)) {
        return got == ref ? ::testing::AssertionSuccess()
                          : ::testing::AssertionFailure() << "expected " << ref << ", got " << got;
    }
    const float diff = std::fabs(got - ref);
    return diff <= tol ? ::testing::AssertionSuccess()
                       : ::testing::AssertionFailure()
                             << got << " vs " << ref << " differ by " << diff << " > " << tol;
}

TEST(SimdMathExtTest, ExtendedUnaryMathFunctionsApplyPerLane) {
    const float4 log10_values = load_vec<float4>(std::array<float, 4>{{1.0f, 10.0f, 100.0f, 1000.0f}});
    const float4 log1p_values = load_vec<float4>(std::array<float, 4>{{0.0f, 1.0f, 3.0f, 7.0f}});
    const float4 cbrt_values = load_vec<float4>(std::array<float, 4>{{1.0f, 8.0f, 27.0f, 64.0f}});
    const float4 trig_values = load_vec<float4>(std::array<float, 4>{{-0.5f, 0.0f, 0.5f, 0.25f}});
    const float4 acosh_values = load_vec<float4>(std::array<float, 4>{{1.0f, 1.5f, 2.0f, 3.0f}});
    const float4 atanh_values = load_vec<float4>(std::array<float, 4>{{0.0f, 0.25f, -0.5f, 0.75f}});

    const auto log10_result = std::simd::log10(log10_values);
    const auto log1p_result = std::simd::log1p(log1p_values);
    const auto log2_result = std::simd::log2(log10_values);
    const auto logb_result = std::simd::logb(log10_values);
    const auto cbrt_result = std::simd::cbrt(cbrt_values);
    const auto asin_result = std::simd::asin(trig_values);
    const auto acos_result = std::simd::acos(trig_values);
    const auto atan_result = std::simd::atan(trig_values);
    const auto tan_result = std::simd::tan(trig_values);
    const auto sinh_result = std::simd::sinh(trig_values);
    const auto cosh_result = std::simd::cosh(trig_values);
    const auto tanh_result = std::simd::tanh(trig_values);
    const auto asinh_result = std::simd::asinh(trig_values);
    const auto acosh_result = std::simd::acosh(acosh_values);
    const auto atanh_result = std::simd::atanh(atanh_values);
    const auto exp2_result = std::simd::exp2(log10_values);
    const auto expm1_result = std::simd::expm1(trig_values);
    const auto erf_result = std::simd::erf(trig_values);
    const auto erfc_result = std::simd::erfc(trig_values);
    const auto lgamma_result = std::simd::lgamma(acosh_values);
    const auto tgamma_result = std::simd::tgamma(log1p_values);

    for (std::simd::simd_size_type i = 0; i < float4::size; ++i) {
        EXPECT_NEAR(log10_result[i], std::log10(log10_values[i]), 1e-6f);
        EXPECT_NEAR(log1p_result[i], std::log1p(log1p_values[i]), 1e-6f);
        EXPECT_NEAR(log2_result[i], std::log2(log10_values[i]), 1e-6f);
        EXPECT_NEAR(logb_result[i], std::logb(log10_values[i]), 1e-6f);
        EXPECT_NEAR(cbrt_result[i], std::cbrt(cbrt_values[i]), 1e-6f);
        EXPECT_NEAR(asin_result[i], std::asin(trig_values[i]), 1e-6f);
        EXPECT_NEAR(acos_result[i], std::acos(trig_values[i]), 1e-6f);
        EXPECT_NEAR(atan_result[i], std::atan(trig_values[i]), 1e-6f);
        EXPECT_NEAR(tan_result[i], std::tan(trig_values[i]), 1e-6f);
        EXPECT_NEAR(sinh_result[i], std::sinh(trig_values[i]), 1e-6f);
        EXPECT_NEAR(cosh_result[i], std::cosh(trig_values[i]), 1e-6f);
        EXPECT_NEAR(tanh_result[i], std::tanh(trig_values[i]), 1e-6f);
        EXPECT_NEAR(asinh_result[i], std::asinh(trig_values[i]), 1e-6f);
        EXPECT_NEAR(acosh_result[i], std::acosh(acosh_values[i]), 1e-6f);
        EXPECT_NEAR(atanh_result[i], std::atanh(atanh_values[i]), 1e-6f);
        EXPECT_TRUE(lane_near(exp2_result[i], std::exp2(log10_values[i]), 1e-6f));
        EXPECT_NEAR(expm1_result[i], std::expm1(trig_values[i]), 1e-6f);
        EXPECT_NEAR(erf_result[i], std::erf(trig_values[i]), 1e-6f);
        EXPECT_NEAR(erfc_result[i], std::erfc(trig_values[i]), 1e-6f);
        EXPECT_NEAR(lgamma_result[i], std::lgamma(acosh_values[i]), 1e-6f);
        EXPECT_TRUE(lane_near(tgamma_result[i], std::tgamma(log1p_values[i]), 1e-6f));
    }
}

TEST(SimdMathExtTest, BinaryMathAndClassificationFunctionsApplyPerLane) {
    const float4 left = load_vec<float4>(std::array<float, 4>{{3.0f, 5.5f, -7.0f, 8.0f}});
    const float4 right = load_vec<float4>(std::array<float, 4>{{4.0f, 2.0f, 2.0f, -3.0f}});
    const float4 signs = load_vec<float4>(std::array<float, 4>{{-1.0f, 1.0f, -1.0f, 1.0f}});
    const float4 classification = load_vec<float4>(std::array<float, 4>{
        {0.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN(), 1.0f / 32.0f}});

    const auto hypot_result = std::simd::hypot(left, 2.0f);
    const auto hypot3_result = std::simd::hypot(left, 2.0f, 3.0f);
    const auto fmod_result = std::simd::fmod(left, 2.0f);
    const auto remainder_result = std::simd::remainder(left, 2.0f);
    const auto copysign_result = std::simd::copysign(left, signs);
    const auto nextafter_result = std::simd::nextafter(left, 100.0f);
    const auto fdim_result = std::simd::fdim(left, right);
    const auto fmin_result = std::simd::fmin(left, right);
    const auto fmax_result = std::simd::fmax(left, right);
    const auto fma_result = std::simd::fma(left, 2.0f, 1.0f);
    const auto atan2_result = std::simd::atan2(left, right);
    const auto lerp_result = std::simd::lerp(left, right, 0.25f);
    const auto ilogb_result = std::simd::ilogb(classification);
    const auto fpclassify_result = std::simd::fpclassify(classification);
    const auto ldexp_result = std::simd::ldexp(left, load_vec<int4>(std::array<int, 4>{{1, 2, -1, 0}}));
    const auto scalbn_result = std::simd::scalbn(left, load_vec<int4>(std::array<int, 4>{{1, 2, -1, 0}}));
    const auto scalbln_result = std::simd::scalbln(left, load_vec<long4>(std::array<long, 4>{{1, 2, -1, 0}}));
    const auto finite_mask = std::simd::isfinite(classification);
    const auto inf_mask = std::simd::isinf(classification);
    const auto nan_mask = std::simd::isnan(classification);
    const auto normal_mask = std::simd::isnormal(classification);
    const auto sign_mask = std::simd::signbit(load_vec<float4>(std::array<float, 4>{{-1.0f, 1.0f, -0.0f, 0.0f}}));
    const auto greater_mask = std::simd::isgreater(left, right);
    const auto unordered_mask = std::simd::isunordered(classification,
        load_vec<float4>(std::array<float, 4>{{0.0f, 0.0f, 0.0f, 0.0f}}));

    for (std::simd::simd_size_type i = 0; i < float4::size; ++i) {
        EXPECT_NEAR(hypot_result[i], std::hypot(left[i], 2.0f), 1e-6f);
        EXPECT_NEAR(hypot3_result[i], std::hypot(left[i], 2.0f, 3.0f), 1e-6f);
        EXPECT_NEAR(fmod_result[i], std::fmod(left[i], 2.0f), 1e-6f);
        EXPECT_NEAR(remainder_result[i], std::remainder(left[i], 2.0f), 1e-6f);
        EXPECT_EQ(copysign_result[i], std::copysign(left[i], signs[i]));
        EXPECT_EQ(nextafter_result[i], std::nextafter(left[i], 100.0f));
        EXPECT_NEAR(fdim_result[i], std::fdim(left[i], right[i]), 1e-6f);
        EXPECT_EQ(fmin_result[i], std::fmin(left[i], right[i]));
        EXPECT_EQ(fmax_result[i], std::fmax(left[i], right[i]));
        EXPECT_NEAR(fma_result[i], std::fma(left[i], 2.0f, 1.0f), 1e-6f);
        EXPECT_NEAR(atan2_result[i], std::atan2(left[i], right[i]), 1e-6f);
        EXPECT_NEAR(lerp_result[i], std::lerp(left[i], right[i], 0.25f), 1e-6f);
        EXPECT_EQ(ilogb_result[i], std::ilogb(classification[i]));
        EXPECT_EQ(fpclassify_result[i], std::fpclassify(classification[i]));
        EXPECT_NEAR(ldexp_result[i], std::ldexp(left[i], std::array<int, 4>{{1, 2, -1, 0}}[static_cast<size_t>(i)]), 1e-6f);
        EXPECT_NEAR(scalbn_result[i], std::scalbn(left[i], std::array<int, 4>{{1, 2, -1, 0}}[static_cast<size_t>(i)]), 1e-6f);
        EXPECT_NEAR(scalbln_result[i], std::scalbln(left[i], std::array<long, 4>{{1, 2, -1, 0}}[static_cast<size_t>(i)]), 1e-6f);
    }

    EXPECT_TRUE(finite_mask[0]);
    EXPECT_FALSE(finite_mask[1]);
    EXPECT_FALSE(finite_mask[2]);
    EXPECT_TRUE(finite_mask[3]);

    EXPECT_FALSE(inf_mask[0]);
    EXPECT_TRUE(inf_mask[1]);
    EXPECT_FALSE(inf_mask[2]);
    EXPECT_FALSE(inf_mask[3]);

    EXPECT_FALSE(nan_mask[0]);
    EXPECT_FALSE(nan_mask[1]);
    EXPECT_TRUE(nan_mask[2]);
    EXPECT_FALSE(nan_mask[3]);

    EXPECT_FALSE(normal_mask[0]);
    EXPECT_FALSE(normal_mask[1]);
    EXPECT_FALSE(normal_mask[2]);
    EXPECT_TRUE(normal_mask[3]);

    EXPECT_TRUE(sign_mask[0]);
    EXPECT_FALSE(sign_mask[1]);
    EXPECT_TRUE(sign_mask[2]);
    EXPECT_FALSE(sign_mask[3]);

    EXPECT_FALSE(greater_mask[0]);
    EXPECT_TRUE(greater_mask[1]);
    EXPECT_FALSE(greater_mask[2]);
    EXPECT_TRUE(greater_mask[3]);

    EXPECT_FALSE(unordered_mask[0]);
    EXPECT_FALSE(unordered_mask[1]);
    EXPECT_TRUE(unordered_mask[2]);
    EXPECT_FALSE(unordered_mask[3]);
}

TEST(SimdMathExtTest, DecompositionFunctionsReturnLaneWiseResults) {
    const float4 frexp_values = load_vec<float4>(std::array<float, 4>{{1.0f, 2.0f, 3.0f, 8.0f}});
    const float4 modf_values = load_vec<float4>(std::array<float, 4>{{1.5f, -2.25f, 3.0f, -4.75f}});
    const float4 remquo_values = load_vec<float4>(std::array<float, 4>{{5.3f, 7.9f, -4.2f, 9.5f}});

    int4 exponents;
    float4 integral;
    int4 quotients;

    const auto fractions = std::simd::frexp(frexp_values, &exponents);
    const auto fractional = std::simd::modf(modf_values, &integral);
    const auto remainders = std::simd::remquo(remquo_values, 2.0f, &quotients);

    for (std::simd::simd_size_type i = 0; i < float4::size; ++i) {
        int expected_exp = 0;
        int expected_quo = 0;
        float expected_integral = 0.0f;

        const float expected_fraction = std::frexp(frexp_values[i], &expected_exp);
        const float expected_fractional = std::modf(modf_values[i], &expected_integral);
        const float expected_remainder = std::remquo(remquo_values[i], 2.0f, &expected_quo);

        EXPECT_NEAR(fractions[i], expected_fraction, 1e-6f);
        EXPECT_EQ(exponents[i], expected_exp);
        EXPECT_NEAR(fractional[i], expected_fractional, 1e-6f);
        EXPECT_NEAR(integral[i], expected_integral, 1e-6f);
        EXPECT_NEAR(remainders[i], expected_remainder, 1e-6f);
        EXPECT_EQ(quotients[i], expected_quo);
    }
}

} // namespace
