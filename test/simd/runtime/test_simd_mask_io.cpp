#include "simd_test_common.hpp"
#include <gtest/gtest.h>
#include <bit>
#include <cstdint>
#include <limits>

#if defined(FORGE_BACKPORT_SIMD_HPP_INCLUDED)
namespace {
namespace ds = std::simd;

template<class V, class Predicate>
constexpr bool check_comparison(const typename V::mask_type& mask, const V& a, const V& b,
                                Predicate predicate) {
    using T = typename V::value_type;
    using Mask = typename V::mask_type;
    const auto vectors = ds::select(mask, a, b);
    const auto scalars = ds::select(mask, T(3), T(4));
    const Mask when_true([](auto i) { return int(i) % 2 == 0; });
    const Mask when_false = !when_true;
    const auto masks = ds::select(mask, when_true, when_false);
    for (int i = 0; i < V::size(); ++i) {
        const bool expected = predicate(a[i], b[i]);
        // Inspect the private storage through the existing internal access struct.
        const auto raw = ds::__detail::mask_access::get(mask, i);
        using Lane = std::remove_cv_t<decltype(raw)>;
        if (raw != (expected ? std::numeric_limits<Lane>::max() : Lane{0}) ||
            mask[i] != expected || vectors[i] != (expected ? a[i] : b[i]) ||
            scalars[i] != (expected ? T(3) : T(4))) return false;
        const bool selected = expected ? when_true[i] : when_false[i];
        if (ds::__detail::mask_access::get(masks, i) !=
            (selected ? std::numeric_limits<Lane>::max() : Lane{0})) return false;
    }
    return true;
}

template<class V>
constexpr bool check_vector_comparisons() {
    using T = typename V::value_type;
    // Three passes include true and false results even for single-lane vectors.
    for (int sample = 0; sample < 3; ++sample) {
        const V a([sample](auto i) { return T((sample + int(i)) % 3); }), b(T(1));
        if (!check_comparison<V>(a == b, a, b, [](T x, T y) { return x == y; }) ||
            !check_comparison<V>(a != b, a, b, [](T x, T y) { return x != y; })) return false;
        if constexpr (std::totally_ordered<T>) {
            if (!check_comparison<V>(a < b, a, b, [](T x, T y) { return x < y; }) ||
                !check_comparison<V>(a <= b, a, b, [](T x, T y) { return x <= y; }) ||
                !check_comparison<V>(a > b, a, b, [](T x, T y) { return x > y; }) ||
                !check_comparison<V>(a >= b, a, b, [](T x, T y) { return x >= y; })) return false;
        }
        if constexpr (requires(T x) { !x; }) {
            if (!check_comparison<V>(!a, a, b, [](T x, T) { return !x; })) return false;
        }
    }
    return true;
}

template<class V>
constexpr bool check_consumers() {
    using T = typename V::value_type;
    using Mask = typename V::mask_type;
    constexpr int n = V::size();
    const V a([](auto i) { return T(1 + int(i) % 3); }), b(T(2));
    const Mask chosen([](auto i) { return int(i) % 2 == 0; }), empty(false);
    const auto selected = ds::select(chosen, a, b);
    const auto scalar = ds::select(chosen, T(1), T(2));
    const V converted = static_cast<V>(chosen);
    const auto equal = a == b, unequal = a != b;
    T sum{};
    for (int i = 0; i < n; ++i) {
        if (selected[i] != (chosen[i] ? a[i] : b[i]) || scalar[i] != (chosen[i] ? T(1) : T(2))) return false;
        if (equal[i] != (a[i] == b[i]) || unequal[i] != (a[i] != b[i])) return false;
        if (converted[i] != (chosen[i] ? T(1) : T(0))) return false;
        if (chosen[i]) sum += a[i];
    }
    if (ds::reduce(a, chosen) != sum || ds::reduce(a, empty, std::plus<>{}, T(9)) != T(9)) return false;
    if constexpr (std::totally_ordered<T>) {
        const auto less = a < b, le = a <= b, greater = a > b, ge = a >= b, zero = !a;
        T lo = a[0], hi = a[0];
        for (int i = 0; i < n; ++i) {
            if (less[i] != (a[i] < b[i]) || le[i] != (a[i] <= b[i]) ||
                greater[i] != (a[i] > b[i]) || ge[i] != (a[i] >= b[i]) || zero[i]) return false;
            if (chosen[i]) { if (a[i] < lo) lo = a[i]; if (a[i] > hi) hi = a[i]; }
        }
        if (ds::reduce_min(a, chosen) != lo || ds::reduce_max(a, chosen) != hi ||
            ds::reduce_min(a, empty) != std::numeric_limits<T>::max() ||
            ds::reduce_max(a, empty) != std::numeric_limits<T>::lowest()) return false;
    }
    std::array<T, n> input{}, output{};
    for (int i = 0; i < n; ++i) { input[i] = a[i]; output[i] = T(9); }
    const V loaded = ds::unchecked_load<V>(input.data(), chosen);
    const V partial = ds::partial_load<V>(input.data(), n - 1, chosen);
    const V constructed(input, chosen);
    ds::unchecked_store(a, output.data(), chosen);
    for (int i = 0; i < n; ++i) {
        if (loaded[i] != (chosen[i] ? a[i] : T{}) || constructed[i] != loaded[i] ||
            partial[i] != (chosen[i] && i < n - 1 ? a[i] : T{}) ||
            output[i] != (chosen[i] ? a[i] : T(9))) return false;
    }
    for (auto& lane : output) lane = T(9);
    ds::partial_store(a, output.data(), n - 1, chosen);
    for (int i = 0; i < n; ++i)
        if (output[i] != (chosen[i] && i < n - 1 ? a[i] : T(9))) return false;
    // Inactive gather/scatter indices are deliberately invalid.
    using Indices = ds::vec<int, n>;
    const Indices indices([n](auto i) { return int(i) % 2 == 0 ? n - 1 - int(i) : -1; });
    const typename Indices::mask_type index_mask(chosen);
    const auto gathered = ds::partial_gather_from<V>(input.data(), n, index_mask, indices);
    const auto unchecked = ds::unchecked_gather_from<V>(input.data(), index_mask, indices);
    for (int i = 0; i < n; ++i)
        if (gathered[i] != (chosen[i] ? input[n - 1 - i] : T{}) || unchecked[i] != gathered[i]) return false;
    for (auto& lane : output) lane = T(9);
    ds::partial_scatter_to(a, output.data(), n, index_mask, indices);
    for (int i = 0; i < n; ++i)
        if (output[n - 1 - i] != (chosen[i] ? a[i] : T(9))) return false;
    for (auto& lane : output) lane = T(9);
    ds::unchecked_scatter_to(a, output.data(), index_mask, indices);
    for (int i = 0; i < n; ++i)
        if (output[n - 1 - i] != (chosen[i] ? a[i] : T(9))) return false;
    return true;
}

template<class T, int N>
void check_type() {
    using V = ds::vec<T, N>;
    static_assert(check_consumers<V>());
    static_assert(check_vector_comparisons<V>());
    EXPECT_TRUE(check_consumers<V>());
    EXPECT_TRUE(check_vector_comparisons<V>());
}

template<int... N>
void check_types(std::integer_sequence<int, N...>) {
    (check_type<signed char, N>(), ...);
    (check_type<short, N>(), ...);
    (check_type<int, N>(), ...);
    (check_type<long long, N>(), ...);
    (check_type<float, N>(), ...);
    (check_type<double, N>(), ...);
    (check_type<std::complex<float>, N>(), ...);
    (check_type<std::complex<double>, N>(), ...);
}

TEST(SimdMaskIntegerIO, ValuesAndMemoryRemainConstexpr) {
    check_types(std::integer_sequence<int, 1, 2, 3, 4, 7, 8, 9, 15, 16, 17, 32, 64>{});
    EXPECT_TRUE(check_consumers<ds::basic_vec<float>>());
    EXPECT_TRUE(check_consumers<ds::basic_vec<double>>());
    EXPECT_TRUE(check_consumers<ds::basic_vec<std::complex<double>>>());
}

template<class T, class U, int N>
constexpr bool check_select_bits(U quiet_nan, U negative_zero, std::uint64_t pattern) {
    using V = ds::vec<T, N>;
    const std::array<U, 4> bits{quiet_nan, negative_zero, U{1}, U{0}};
    const V a([&bits](auto i) { return std::bit_cast<T>(bits[int(i) % 4]); });
    const V b([&bits](auto i) { return std::bit_cast<T>(bits[(int(i) + 1) % 4]); });
    const typename V::mask_type mask(pattern);
    const auto selected = ds::select(mask, a, b);
    const auto scalars = ds::select(mask, std::bit_cast<T>(quiet_nan), std::bit_cast<T>(negative_zero));
    for (int i = 0; i < N; ++i) {
        if (std::bit_cast<U>(selected[i]) != bits[(i + (mask[i] ? 0 : 1)) % 4] ||
            std::bit_cast<U>(scalars[i]) != (mask[i] ? quiet_nan : negative_zero)) return false;
    }
    return true;
}

template<class T, class U, int N>
void check_select_paths() {
    constexpr U quiet_nan = sizeof(U) == 4 ? static_cast<U>(0x7fc12345u) :
        static_cast<U>(0x7ff8123456789abcull);
    constexpr U negative_zero = U{1} << (sizeof(U) * 8 - 1);
    static_assert(check_select_bits<T, U, N>(quiet_nan, negative_zero, 0x5555555555555555ull));
    volatile std::uint64_t pattern = 0xaaaaaaaaaaaaaaaaull;
    EXPECT_TRUE((check_select_bits<T, U, N>(quiet_nan, negative_zero, pattern)));
}

template<int... N>
void check_select_widths(std::integer_sequence<int, N...>) {
    (check_select_paths<float, std::uint32_t, N>(), ...);
    (check_select_paths<double, std::uint64_t, N>(), ...);
}

TEST(SimdMaskIntegerIO, ConditionalAndBlendPreserveFloatingBits) {
    check_select_widths(std::integer_sequence<int, 1, 2, 3, 4, 7, 8, 9, 15, 16, 17>{});
}

template<class T, class U>
void check_bits(U quiet_nan, U negative_zero, U positive_infinity) {
    using V = ds::vec<T, 7>;
    const std::array<U, 7> a_bits{quiet_nan, negative_zero, positive_infinity, 1, 0, quiet_nan + 1, negative_zero};
    const std::array<U, 7> b_bits{negative_zero, quiet_nan + 2, 0, positive_infinity, 1, negative_zero, quiet_nan + 3};
    const V a([&a_bits](auto i) { return std::bit_cast<T>(a_bits[int(i)]); });
    const V b([&b_bits](auto i) { return std::bit_cast<T>(b_bits[int(i)]); });
    const typename V::mask_type chosen(0x55u);
    const auto result = ds::select(chosen, a, b);
    for (int i = 0; i < 7; ++i)
        EXPECT_EQ(std::bit_cast<U>(result[i]), chosen[i] ? a_bits[i] : b_bits[i]);
    // The first selected NaN and the sign of a selected zero survive reductions.
    const typename V::mask_type first(1u), second(2u);
    EXPECT_EQ(std::bit_cast<U>(ds::reduce(a, first)), quiet_nan);
    EXPECT_EQ(std::bit_cast<U>(ds::reduce_min(a, first)), quiet_nan);
    EXPECT_EQ(std::bit_cast<U>(ds::reduce_max(a, first)), quiet_nan);
    EXPECT_EQ(std::bit_cast<U>(ds::reduce(a, second)), negative_zero);
    EXPECT_EQ(std::bit_cast<U>(ds::reduce_min(a, second)), negative_zero);
    EXPECT_EQ(std::bit_cast<U>(ds::reduce_max(a, second)), negative_zero);
    const auto lt = a < b, le = a <= b, gt = a > b, ge = a >= b, eq = a == b, ne = a != b;
    for (int i = 0; i < 7; ++i) {
        EXPECT_EQ(lt[i], a[i] < b[i]); EXPECT_EQ(le[i], a[i] <= b[i]);
        EXPECT_EQ(gt[i], a[i] > b[i]); EXPECT_EQ(ge[i], a[i] >= b[i]);
        EXPECT_EQ(eq[i], a[i] == b[i]); EXPECT_EQ(ne[i], a[i] != b[i]);
    }
}

TEST(SimdMaskIntegerIO, SelectAndReducePreserveFloatingBits) {
    check_bits<float, std::uint32_t>(0x7fc12345u, 0x80000000u, 0x7f800000u);
    check_bits<double, std::uint64_t>(0x7ff8123456789abcull, 0x8000000000000000ull, 0x7ff0000000000000ull);
}
} // namespace
#endif
