#include "simd_test_common.hpp"

#include <gtest/gtest.h>

#include <array>
#include <concepts>
#include <cstdlib>
#include <exception>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace {

using namespace simd_test;

// Lvalue conversions keep the source unchanged and return a stable value.
template<class T>
struct stable_numeric_source {
    T payload;
    int* conversions;

    constexpr operator T() const& noexcept {
        ++*conversions;
        return payload;
    }
};

template<class T>
struct consuming_numeric_source {
    T payload;
    int* conversions;

    constexpr operator T() && noexcept {
        ++*conversions;
        return std::exchange(payload, T{});
    }
};

template<int Value>
struct counted_constant {
    static constexpr int value = Value;
    inline static int conversions = 0;

    constexpr operator int() const noexcept {
        if (!std::is_constant_evaluated()) {
            ++conversions;
        }
        return value;
    }
};

struct counted_generator {
    int* conversions;
    int* calls;
    std::array<int, 4>* order;

    constexpr operator int() const noexcept {
        ++*conversions;
        return 77;
    }

    template<class Index>
    constexpr int operator()(Index) & noexcept {
        (*order)[(*calls)++] = static_cast<int>(Index::value);
        return static_cast<int>(Index::value * 3 + 1);
    }
};

struct throwing_numeric_source {
    int* conversions;
    bool fail;

    operator int() const {
        ++*conversions;
        if (fail) {
            throw std::runtime_error("scalar conversion");
        }
        return 31;
    }
};

struct throwing_generator {
    int* calls;

    template<class Index>
    int operator()(Index) const {
        ++*calls;
        if constexpr (Index::value == 2) {
            throw std::runtime_error("generator call");
        }
        return static_cast<int>(Index::value + 1);
    }
};

struct throwing_conversion_generator {
    int* calls;
    int* conversions;

    template<class Index>
    throwing_numeric_source operator()(Index) const noexcept {
        ++*calls;
        return {conversions, Index::value == 2};
    }
};

static_assert(std::convertible_to<stable_numeric_source<int>&, int>);
static_assert(std::convertible_to<const stable_numeric_source<int>&, int>);
static_assert(std::convertible_to<consuming_numeric_source<int>, int>);
static_assert(std::convertible_to<consuming_numeric_source<float>, float>);
static_assert(std::is_convertible_v<consuming_numeric_source<int>, int4>);
static_assert(!std::is_constructible_v<int4, consuming_numeric_source<int>&>);
static_assert(!std::is_constructible_v<int4, const consuming_numeric_source<int>&>);
static_assert(!std::is_constructible_v<int4, const consuming_numeric_source<int>&&>);

static_assert(std::is_convertible_v<short, int4>);
static_assert(std::is_convertible_v<bool, int4>);
static_assert(std::is_convertible_v<float, float4>);
static_assert(!std::is_constructible_v<int4, double>);
static_assert(!std::is_constructible_v<float4, int>);
static_assert(!std::is_constructible_v<uint4, int>);

static_assert(std::is_convertible_v<counted_constant<16'777'216>, float4>);
static_assert(!std::is_constructible_v<float4, counted_constant<16'777'217>>);
static_assert(!std::is_constructible_v<uint4, counted_constant<-1>>);
static_assert(std::is_convertible_v<std::integral_constant<double, 1.0>, int4>);
static_assert(!std::is_constructible_v<int4, std::integral_constant<double, 1.5>>);
static_assert(std::is_convertible_v<wrapper_bad_value, int4>);
static_assert(!std::is_constructible_v<int4, explicit_to_int>);
static_assert(!std::is_constructible_v<int4, std::nullptr_t>);

static_assert(std::is_constructible_v<int4, counted_generator>);
static_assert(!std::is_convertible_v<counted_generator, int4>);
static_assert(std::is_nothrow_constructible_v<int4, counted_generator>);
static_assert(!std::is_constructible_v<float4, int_generator>);
static_assert(std::is_nothrow_constructible_v<int4, throwing_numeric_source>);
static_assert(!std::is_nothrow_constructible_v<int4, throwing_generator>);
static_assert(!std::is_nothrow_constructible_v<int4, throwing_conversion_generator>);

template<class V>
constexpr bool all_lanes_equal(const V& values, typename V::value_type expected) {
    for (std::simd::simd_size_type i = 0; i < V::size; ++i) {
        if (values[i] != expected) {
            return false;
        }
    }
    return true;
}

constexpr bool constexpr_broadcast_converts_once() {
    int stable_conversions = 0;
    const stable_numeric_source<int> stable{19, &stable_conversions};
    const int4 stable_values = stable;

    int consuming_conversions = 0;
    consuming_numeric_source<float> consuming{2.5f, &consuming_conversions};
    const float4 consuming_values = std::move(consuming);

    const int4 arithmetic = short{7};
    const float4 wrapped = counted_constant<16'777'216>{};
    const int4 non_wrapper = wrapper_bad_value{};

    return stable_conversions == 1 && stable.payload == 19 &&
           all_lanes_equal(stable_values, 19) && consuming_conversions == 1 &&
           consuming.payload == 0.0f && all_lanes_equal(consuming_values, 2.5f) &&
           all_lanes_equal(arithmetic, 7) && all_lanes_equal(wrapped, 16'777'216.0f) &&
           all_lanes_equal(non_wrapper, 6);
}

constexpr bool constexpr_generator_is_unchanged() {
    int conversions = 0;
    int calls = 0;
    std::array<int, 4> order{};
    const int4 values(counted_generator{&conversions, &calls, &order});
    const int4 indices(index_object_generator{});

    if (conversions != 0 || calls != 4) {
        return false;
    }
    for (std::simd::simd_size_type i = 0; i < int4::size; ++i) {
        if (order[i] != i || values[i] != i * 3 + 1 || indices[i] != i) {
            return false;
        }
    }
    return true;
}

static_assert(constexpr_broadcast_converts_once());
static_assert(constexpr_generator_is_unchanged());

template<class V>
void expect_stable_conversion_once(typename V::value_type expected) {
    int conversions = 0;
    stable_numeric_source<typename V::value_type> source{expected, &conversions};
    const auto& constant = source;

    const V from_lvalue = source;
    EXPECT_EQ(conversions, 1);
    EXPECT_TRUE(all_lanes_equal(from_lvalue, expected));

    const V from_const_lvalue = constant;
    EXPECT_EQ(conversions, 2);
    EXPECT_TRUE(all_lanes_equal(from_const_lvalue, expected));

    const V from_rvalue = std::move(source);
    EXPECT_EQ(conversions, 3);
    EXPECT_TRUE(all_lanes_equal(from_rvalue, expected));

    const V from_const_rvalue = std::move(constant);
    EXPECT_EQ(conversions, 4);
    EXPECT_TRUE(all_lanes_equal(from_const_rvalue, expected));
    EXPECT_EQ(source.payload, expected);
}

template<class V>
void expect_consuming_conversion_once(typename V::value_type expected) {
    int conversions = 0;
    consuming_numeric_source<typename V::value_type> source{expected, &conversions};
    const V values = std::move(source);

    EXPECT_EQ(conversions, 1);
    EXPECT_EQ(source.payload, typename V::value_type{});
    EXPECT_TRUE(all_lanes_equal(values, expected));
}

TEST(SimdBroadcastTest, StableNumericConversionsAreIndependentOfWidthAndCvref) {
    expect_stable_conversion_once<int1>(19);
    expect_stable_conversion_once<int4>(19);
    expect_stable_conversion_once<int8>(19);
    expect_stable_conversion_once<float4>(2.5f);
}

TEST(SimdBroadcastTest, RvalueConversionConsumesOnlyOnceForAllLanes) {
    expect_consuming_conversion_once<int1>(19);
    expect_consuming_conversion_once<int4>(19);
    expect_consuming_conversion_once<int8>(19);
    expect_consuming_conversion_once<float4>(2.5f);
}

TEST(SimdBroadcastTest, ImplicitConditionsPreserveArithmeticAndWrapperValues) {
    const int4 arithmetic = short{7};
    const int4 boolean = true;
    const float4 floating = 2.5f;
    const int4 non_wrapper = wrapper_bad_value{};
    const int4 floating_wrapper = std::integral_constant<double, 1.0>{};

    counted_constant<16'777'216>::conversions = 0;
    const float4 wrapped = counted_constant<16'777'216>{};

    EXPECT_TRUE(all_lanes_equal(arithmetic, 7));
    EXPECT_TRUE(all_lanes_equal(boolean, 1));
    EXPECT_TRUE(all_lanes_equal(floating, 2.5f));
    EXPECT_TRUE(all_lanes_equal(non_wrapper, 6));
    EXPECT_TRUE(all_lanes_equal(floating_wrapper, 1));
    EXPECT_TRUE(all_lanes_equal(wrapped, 16'777'216.0f));
    EXPECT_EQ(counted_constant<16'777'216>::conversions, 1);
}

TEST(SimdBroadcastTest, CallableScalarStillUsesGeneratorInLaneOrder) {
    int conversions = 0;
    int calls = 0;
    std::array<int, 4> order{};
    counted_generator generator{&conversions, &calls, &order};
    const int4 values(generator);

    EXPECT_EQ(conversions, 0);
    EXPECT_EQ(calls, 4);
    for (std::simd::simd_size_type i = 0; i < int4::size; ++i) {
        EXPECT_EQ(order[i], i);
        EXPECT_EQ(values[i], i * 3 + 1);
    }
}

TEST(SimdBroadcastTest, PotentiallyThrowingScalarConvertsOnceOnSuccess) {
    int conversions = 0;
    const int4 values = throwing_numeric_source{&conversions, false};

    EXPECT_EQ(conversions, 1);
    EXPECT_TRUE(all_lanes_equal(values, 31));
}

TEST(SimdBroadcastTest, ThrowingGeneratorStillPropagatesAndStopsAtFailingLane) {
    int calls = 0;
    EXPECT_THROW((void)int4(throwing_generator{&calls}), std::runtime_error);
    EXPECT_EQ(calls, 3);
}

TEST(SimdBroadcastTest, ThrowingGeneratedConversionStillPropagates) {
    int calls = 0;
    int conversions = 0;
    EXPECT_THROW((void)int4(throwing_conversion_generator{&calls, &conversions}),
        std::runtime_error);
    EXPECT_EQ(calls, 3);
    EXPECT_EQ(conversions, 3);
}

TEST(SimdBroadcastDeathTest, ThrowingScalarStillTerminatesUnderNoexcept) {
#ifdef GTEST_HAS_DEATH_TEST
    EXPECT_EXIT(
        {
            std::set_terminate([] { std::_Exit(86); });
            int conversions = 0;
            const int4 values(throwing_numeric_source{&conversions, true});
            (void)values;
            std::_Exit(87);
        },
        ::testing::ExitedWithCode(86), "");
#else
    GTEST_SKIP() << "Death tests are unavailable on this platform.";
#endif
}

} // namespace
