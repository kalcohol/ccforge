#include "simd_test_common.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <complex>
#include <concepts>
#include <functional>
#include <utility>

namespace {

template<class T>
concept scalar_reduce_forms = requires(const T& value, bool selected) {
    { std::simd::reduce(value) } -> std::same_as<T>;
    { std::simd::reduce(value, std::plus<>{}) } -> std::same_as<T>;
    { std::simd::reduce(value, selected) } -> std::same_as<T>;
    { std::simd::reduce(value, selected, std::multiplies<>{}) } -> std::same_as<T>;
    { std::simd::reduce(value, selected, std::plus<>{}, T{}) } -> std::same_as<T>;
};

static_assert(scalar_reduce_forms<int>);
static_assert(scalar_reduce_forms<float>);
static_assert(scalar_reduce_forms<double>);
static_assert(scalar_reduce_forms<std::complex<float>>);
static_assert(scalar_reduce_forms<std::complex<double>>);

struct counting_plus {
    int* calls;

    template<class T, class Abi>
    constexpr std::simd::basic_vec<T, Abi> operator()(
        const std::simd::basic_vec<T, Abi>& left,
        const std::simd::basic_vec<T, Abi>& right) const noexcept {
        ++*calls;
        return left + right;
    }
};

template<class T, class Op>
concept accepts_scalar_operation = requires(const T& value, Op op) {
    std::simd::reduce(value, op);
    std::simd::reduce(value, false, op, T{});
};

static_assert(accepts_scalar_operation<std::complex<double>, counting_plus>);
static_assert(!accepts_scalar_operation<std::complex<double>, std::plus<std::complex<double>>>);

template<class T, class Mask>
concept accepts_scalar_mask = requires(const T& value, Mask mask) {
    std::simd::reduce(value, mask, std::plus<>{}, T{});
};

static_assert(accepts_scalar_mask<std::complex<double>, bool>);
static_assert(!accepts_scalar_mask<std::complex<double>, int>);

template<class T>
constexpr bool complex_reductions_are_constexpr() {
    using C = std::complex<T>;
    const C value(T{3}, T{-2});
    int calls = 0;
    const counting_plus op{&calls};
    return std::simd::reduce(value) == value &&
        std::simd::reduce(std::move(value), op) == value &&
        std::simd::reduce(value, true) == value &&
        std::simd::reduce(value, false) == C{} &&
        std::simd::reduce(value, false, std::multiplies<>{}) == C(T{1}) &&
        std::simd::reduce(value, true, op, C{}) == value &&
        std::simd::reduce(value, false, op, C{}) == C{} && calls == 0;
}

static_assert(complex_reductions_are_constexpr<float>());
static_assert(complex_reductions_are_constexpr<double>());

TEST(SimdReduceScalarComplex, ComplexValuesAndDefaultIdentities) {
    const std::complex<double> value(3.0, -2.0);
    EXPECT_EQ(std::simd::reduce(value), value);
    EXPECT_EQ(std::simd::reduce(std::as_const(value), true), value);
    EXPECT_EQ(std::simd::reduce(std::move(value)), value);
    EXPECT_EQ(std::simd::reduce(value, false), std::complex<double>{});
    EXPECT_EQ(std::simd::reduce(value, false, std::multiplies<>{}), std::complex<double>(1.0));
    EXPECT_EQ(std::simd::reduce(value, false, std::plus<>{}, 0), std::complex<double>{});
}

TEST(SimdReduceScalarComplex, ScalarReductionDoesNotInvokeTheReducer) {
    const std::complex<float> value(2.0f, 1.0f);
    int calls = 0;
    const counting_plus op{&calls};
    EXPECT_EQ(std::simd::reduce(value, op), value);
    EXPECT_EQ(std::simd::reduce(value, true, op, std::complex<float>{}), value);
    EXPECT_EQ(std::simd::reduce(value, false, op, std::complex<float>{}), std::complex<float>{});
    EXPECT_EQ(calls, 0);
}

TEST(SimdReduceScalarComplex, ScalarAndVectorControlsPreserveTypesAndSignedZero) {
    const std::complex<double> value(-0.0, -0.0);
    const auto reduced = std::simd::reduce(value, true);
    EXPECT_TRUE(std::signbit(reduced.real()));
    EXPECT_TRUE(std::signbit(reduced.imag()));
    const simd_test::complex4d values(value);
    EXPECT_EQ(std::simd::reduce(values), std::complex<double>(-0.0, -0.0));
    EXPECT_EQ(std::simd::reduce(7), 7);
    EXPECT_EQ(std::simd::reduce(7, false), 0);
    EXPECT_EQ(std::simd::reduce(7.0, false, std::multiplies<>{}), 1.0);
}

} // namespace
