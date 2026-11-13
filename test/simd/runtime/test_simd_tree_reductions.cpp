#include "simd_test_common.hpp"
#include <gtest/gtest.h>
#include <cmath>
#include <functional>
#include <limits>

namespace reduction_adl_collision {
struct plus {
    template<class V> constexpr V operator()(const V& a, const V& b) const {
        return a + b;
    }
};

// ADL would prefer this overload over the internal recursive helper.
template<std::simd::simd_size_type First, std::simd::simd_size_type Count, class Get>
auto reduction_tree(Get&, plus&) = delete;
} // namespace reduction_adl_collision

namespace {
constexpr bool collision_reduction() {
    const std::simd::vec<int, 5> value([](auto i) { return int(i) + 1; });
    const auto selected = value > decltype(value)(2);
    return std::simd::reduce(value, reduction_adl_collision::plus{}) == 15 &&
        std::simd::reduce(value, selected, reduction_adl_collision::plus{}, 0) == 12;
}
static_assert(collision_reduction());

TEST(SimdTreeReductions, InternalHelpersAvoidADL) {
    EXPECT_TRUE(collision_reduction());
}

struct minimum {
    template<class V> constexpr V operator()(const V& a, const V& b) const {
        return std::simd::select(a < b, a, b);
    }
};
struct maximum {
    template<class V> constexpr V operator()(const V& a, const V& b) const {
        return std::simd::select(a > b, a, b);
    }
};

template<int N> void check_width() {
    using V = std::simd::basic_vec<int, std::simd::fixed_size_abi<N>>;
    const V value([](auto i) { return int(i) % 7 + 1; });
    const V product([](auto i) { return int(i) == 0 ? 2 : 1; });
    auto check = [&](const typename V::mask_type& mask) {
        int sum = 0, mul = 1, bits_and = ~0, bits_or = 0, bits_xor = 0;
        int lo = std::numeric_limits<int>::max(), hi = std::numeric_limits<int>::lowest();
        int count = 0;
        for (int i = 0; i < N; ++i) if (mask[i]) {
            ++count;
            sum += value[i]; mul *= product[i];
            bits_and &= value[i]; bits_or |= value[i]; bits_xor ^= value[i];
            lo = std::min(lo, int(value[i])); hi = std::max(hi, int(value[i]));
        }
        EXPECT_EQ(std::simd::reduce(value, mask), sum);
        EXPECT_EQ(std::simd::reduce(product, mask, std::multiplies<>{}), mul);
        EXPECT_EQ(std::simd::reduce(value, mask, std::bit_and<>{}), bits_and);
        EXPECT_EQ(std::simd::reduce(value, mask, std::bit_or<>{}), bits_or);
        EXPECT_EQ(std::simd::reduce(value, mask, std::bit_xor<>{}), bits_xor);
        EXPECT_EQ(std::simd::reduce(value, mask, minimum{}, std::numeric_limits<int>::max()), lo);
        EXPECT_EQ(std::simd::reduce(value, mask, maximum{}, std::numeric_limits<int>::lowest()), hi);
        EXPECT_EQ(std::simd::reduce_min(value, mask), lo);
        EXPECT_EQ(std::simd::reduce_max(value, mask), hi);
        EXPECT_EQ(std::simd::reduce_count(mask), count);
        EXPECT_EQ(std::simd::all_of(mask), count == N);
        EXPECT_EQ(std::simd::any_of(mask), count != 0);
        EXPECT_EQ(std::simd::none_of(mask), count == 0);
    };
    check(typename V::mask_type(false));
    check(typename V::mask_type(true));
    for (int count = 1; count <= N; ++count) {
        check(value <= V(count));
        const typename V::mask_type suffix([&](auto i) { return int(i) >= N - count; });
        check(suffix);
    }
    const typename V::mask_type alternating([](auto i) { return int(i) % 2 == 0; });
    check(alternating);
    int sum = 0, bits_and = ~0, bits_or = 0, bits_xor = 0;
    for (int i = 0; i < N; ++i) {
        sum += value[i]; bits_and &= value[i]; bits_or |= value[i]; bits_xor ^= value[i];
    }
    EXPECT_EQ(std::simd::reduce(value), sum);
    EXPECT_EQ(std::simd::reduce(product, std::multiplies<>{}), 2);
    EXPECT_EQ(std::simd::reduce(value, std::bit_and<>{}), bits_and);
    EXPECT_EQ(std::simd::reduce(value, std::bit_or<>{}), bits_or);
    EXPECT_EQ(std::simd::reduce(value, std::bit_xor<>{}), bits_xor);
    EXPECT_EQ(std::simd::reduce(value, minimum{}), 1);
    EXPECT_EQ(std::simd::reduce(value, maximum{}), std::min(N, 7));
    EXPECT_EQ(std::simd::reduce_min(value), 1);
    EXPECT_EQ(std::simd::reduce_max(value), std::min(N, 7));
    auto floating = [&]<class T>() {
        using F = std::simd::basic_vec<T, std::simd::fixed_size_abi<N>>;
        const F x([](auto i) { return T(0.1) * T(int(i) + 1); });
        long double expected = 0, selected = 0;
        const typename F::mask_type mask([](auto i) { return int(i) % 2 == 0; });
        for (int i = 0; i < N; ++i) {
            expected += x[i];
            if (mask[i]) selected += x[i];
        }
        const auto tolerance = double(std::numeric_limits<T>::epsilon() * N * expected);
        EXPECT_NEAR(std::simd::reduce(x), double(expected), tolerance);
        EXPECT_NEAR(std::simd::reduce(x, mask), double(selected), tolerance);
        EXPECT_EQ(std::simd::reduce_min(x), x[0]);
        EXPECT_EQ(std::simd::reduce_max(x), x[N - 1]);
        const F zero(-T(0));
        EXPECT_TRUE(std::signbit(std::simd::reduce_min(zero)));
        EXPECT_TRUE(std::signbit(std::simd::reduce_max(zero)));
        const typename F::mask_type single([](auto i) { return int(i) == N - 1; });
        EXPECT_TRUE(std::signbit(std::simd::reduce(zero, single)));
    };
    floating.template operator()<float>();
    floating.template operator()<double>();
}

template<std::size_t... I> void widths(std::index_sequence<I...>) {
    (check_width<int(I) + 1>(), ...);
}
TEST(SimdTreeReductions, EverySupportedFixedWidth) {
    widths(std::make_index_sequence<64>{});
}

constexpr bool constant_reductions() {
    const std::simd::vec<int, 5> x([](auto i) { return int(i) + 1; });
    const auto mask = x > decltype(x)(2);
    return std::simd::reduce(x) == 15 && std::simd::reduce(x, mask) == 12 &&
        std::simd::reduce_min(x) == 1 && std::simd::reduce_max(x, mask) == 5 &&
        std::simd::reduce_count(mask) == 3;
}
static_assert(constant_reductions());

TEST(SimdTreeReductions, InfinityAndUnorderedCandidates) {
    using V = std::simd::vec<double, 5>;
    const double inf = std::numeric_limits<double>::infinity();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const typename V::mask_type all(true);
    EXPECT_EQ(std::simd::reduce_min(V(inf), all), inf);
    EXPECT_EQ(std::simd::reduce_max(V(-inf), all), -inf);
    const double lanes[]{3, 4, nan, 1, 2};
    const V x([&](auto i) { return lanes[int(i)]; });
    EXPECT_EQ(std::simd::reduce_min(x), 1);
    EXPECT_EQ(std::simd::reduce_max(x), 4);
    EXPECT_EQ(std::simd::reduce_min(x, all), 1);
    EXPECT_EQ(std::simd::reduce_max(x, all), 4);
    const V first_nan([&](auto i) { return int(i) == 0 ? nan : double(int(i)); });
    EXPECT_TRUE(std::isnan(std::simd::reduce_min(first_nan)));
    EXPECT_TRUE(std::isnan(std::simd::reduce_max(first_nan, all)));
}
} // namespace
