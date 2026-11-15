#include "simd_test_common.hpp"
#include <gtest/gtest.h>
#include <bit>
#include <complex>
#include <cstdint>
#include <functional>
#include <limits>
#include <type_traits>

namespace {
namespace ds = std::simd;

template<std::size_t Bytes, int N>
using M = ds::basic_mask<Bytes, ds::__fixed_size_abi<N>>;

template<class Mask>
concept has_unary_vectors = requires(const Mask& value) { +value; -value; ~value; };

template<class Mask>
constexpr bool normalized(const Mask& value) {
    for (int i = 0; i < Mask::size(); ++i) {
        const auto bits = ds::__detail::mask_lane(value, i);
        if (bits != 0 && bits != std::numeric_limits<std::remove_cv_t<decltype(bits)>>::max()) return false;
        if ((bits != 0) != value[i]) return false;
    }
    return true;
}

template<class Destination, class Source>
constexpr bool check_lane_copy(const Source& source) {
    Destination destination;
    for (int i = 0; i < Source::size(); ++i)
        ds::__detail::copy_lane(destination, i, source, i);
    if (!normalized(destination)) return false;
    for (int i = 0; i < Source::size(); ++i)
        if (destination[i] != source[i]) return false;
    return true;
}

template<class Part, class Mask>
constexpr bool check_chunk_part(const Part& part, const Mask& whole, int& offset) {
    if (!normalized(part)) return false;
    for (int i = 0; i < part.size(); ++i)
        if (part[i] != whole[offset++]) return false;
    return true;
}

template<std::size_t Bytes, int N>
constexpr bool check_mask(std::uint64_t x, std::uint64_t y) {
    using Mask = M<Bytes, N>;
    using Lane = ds::__detail::mask_lane_t<Bytes>;
    static_assert(sizeof(Mask) == sizeof(Lane) * N);
    static_assert(alignof(Mask) == alignof(Lane));
    static_assert(std::is_trivially_copyable_v<Mask>);
    static_assert(std::same_as<decltype(std::declval<Mask>()[0]), bool>);
    static_assert(std::is_nothrow_constructible_v<Mask, bool>);
    static_assert(has_unary_vectors<Mask> == (Bytes <= 8));
    const Mask a(x), b(y), empty, full(true);
    const Mask generated([x](auto i) { return ((x >> int(i)) & 1u) != 0; });
    const Mask masks[]{a, b, !a, a && b, a || b, a & b, a | b, a ^ b,
        a == b, a != b, a < b, a <= b, a > b, a >= b,
        ds::select(a, b, !b), ds::select(a, true, false), ds::select(a, false, true),
        ds::select(a, true, true), ds::select(a, false, false)};
    for (const auto& value : masks) if (!normalized(value)) return false;
    Mask c = a;
    c &= b;
    if (!ds::all_of(c == (a & b))) return false;
    c = a; c |= b;
    if (!ds::all_of(c == (a | b))) return false;
    c = a; c ^= b;
    if (!ds::all_of(c == (a ^ b))) return false;
    if (!normalized(c) || !normalized(generated) || !normalized(full) || !normalized(empty)) return false;
    c = a; c &= c;
    if (!ds::all_of(c == a)) return false;
    c |= c;
    if (!ds::all_of(c == a)) return false;
    c ^= c;
    if (!ds::none_of(c) || !normalized(c)) return false;
    int count = 0, first = 0, last = 0;
    for (int i = 0; i < N; ++i) {
        const bool p = ((x >> i) & 1u) != 0, q = ((y >> i) & 1u) != 0;
        const bool expected[]{p, q, !p, p && q, p || q, p && q, p || q, p != q,
            p == q, p != q, p < q, p <= q, p > q, p >= q,
            p ? q : !q, p, !p, true, false};
        for (std::size_t j = 0; j < std::size(masks); ++j)
            if (masks[j][i] != expected[j]) return false;
        if (generated[i] != p || empty[i] || !full[i]) return false;
        if (p) { if (count == 0) first = i; last = i; ++count; }
    }
    if (ds::reduce_count(a) != count || ds::all_of(a) != (count == N) ||
        ds::any_of(a) != (count != 0) || ds::none_of(a) != (count == 0)) return false;
    // Index reductions require at least one selected lane.
    if (count != 0 && (ds::reduce_min_index(a) != first || ds::reduce_max_index(a) != last)) return false;
    const auto truncated = x & (~std::uint64_t{0} >> (64 - N));
    if (a.to_ullong() != truncated) return false;
    // Every source/destination pair is exercised as Bytes ranges over 1,2,4,8,16.
    const M<1, N> m1(a); const M<2, N> m2(a); const M<4, N> m4(a);
    const M<8, N> m8(a); const M<16, N> m16(a);
    if (!normalized(m1) || !normalized(m2) || !normalized(m4) || !normalized(m8) || !normalized(m16)) return false;
    if (!check_lane_copy<M<1, N>>(a) || !check_lane_copy<M<2, N>>(a) ||
        !check_lane_copy<M<4, N>>(a) || !check_lane_copy<M<8, N>>(a) ||
        !check_lane_copy<M<16, N>>(a)) return false;
    if (!ds::all_of(Mask(m1) == a) || !ds::all_of(Mask(m2) == a) ||
        !ds::all_of(Mask(m4) == a) || !ds::all_of(Mask(m8) == a) || !ds::all_of(Mask(m16) == a)) return false;
    if constexpr (Bytes <= 8) {
        const auto positive = +a, negative = -a, inverted = ~a;
        using V = decltype(positive);
        const V converted = a;
        for (int i = 0; i < N; ++i) {
            const int p = a[i] ? 1 : 0;
            if (positive[i] != p || negative[i] != -p || inverted[i] != ~p || converted[i] != p) return false;
        }
    }
    const ds::vec<int, N> wide = static_cast<ds::vec<int, N>>(a);
    for (int i = 0; i < N; ++i) if (wide[i] != int(a[i])) return false;
    const auto packed = ds::compress(a, b, true);
    const auto expanded = ds::expand(packed, b, a);
    if (!normalized(packed) || !normalized(expanded)) return false;
    if (!ds::all_of(expanded == a)) return false;
    const ds::vec<int, N> reversed([](auto i) { return N - 1 - int(i); });
    const auto permuted = a[reversed];
    if (!normalized(permuted)) return false;
    for (int i = 0; i < N; ++i) if (permuted[i] != a[N - 1 - i]) return false;
    const auto mapped = ds::permute(a, [](int lane) { return N - 1 - lane; });
    if (!normalized(mapped)) return false;
    for (int i = 0; i < N; ++i) if (mapped[i] != a[N - 1 - i]) return false;
    const auto sentinels = ds::permute<3>(a, [](int lane) {
        return lane == 0 ? ds::zero_element : lane == 1 ? ds::uninit_element : 0;
    });
    if (!normalized(sentinels) || sentinels[0] || sentinels[1] || sentinels[2] != a[0]) return false;
    if constexpr (N >= 3) {
        const auto split = ds::chunk<2>(a);
        int offset = 0;
        bool valid = true;
        std::apply([&valid, &a, &offset](const auto&... part) {
            valid = (check_chunk_part(part, a, offset) && ...);
        }, split);
        if (!valid || offset != N) return false;
    }
    const auto chunks = ds::chunk<N>(a);
    const auto joined = ds::cat(chunks[0]);
    if (!normalized(joined) || !ds::all_of(Mask(joined) == a)) return false;
    int i = 0;
    for (bool lane : a) if (lane != a[i++]) return false;
    return true;
}

template<std::size_t Bytes, int N>
void check_width() {
    static_assert(check_mask<Bytes, N>(0xa55aa55aa55aa55aull, 0x3c3c3c3c3c3c3c3cull));
    SCOPED_TRACE(Bytes);
    SCOPED_TRACE(N);
    EXPECT_TRUE((check_mask<Bytes, N>(0, ~std::uint64_t{0})));
    EXPECT_TRUE((check_mask<Bytes, N>(~std::uint64_t{0}, 0)));
    EXPECT_TRUE((check_mask<Bytes, N>(std::uint64_t{1} << (N - 1), 1)));
    // Exhaust every lane pattern at the small and odd widths.
    if constexpr (N <= 7) {
        for (std::uint64_t x = 0; x < (std::uint64_t{1} << N); ++x)
            EXPECT_TRUE((check_mask<Bytes, N>(x, 0x55)));
    }
    const M<Bytes, N> original(0xa55aa55aa55aa55aull);
    const std::bitset<N> bits = original.to_bitset();
    const M<Bytes, N> restored = bits;
    EXPECT_TRUE(normalized(restored));
    EXPECT_TRUE(ds::all_of(restored == original));
    const M<Bytes, N> narrow(static_cast<unsigned char>(0xa5));
    EXPECT_TRUE(normalized(narrow));
    EXPECT_TRUE(ds::all_of(narrow == M<Bytes, N>(0xa5ull)));
#if defined(__cpp_lib_constexpr_bitset) && __cpp_lib_constexpr_bitset >= 202207L
    static_assert([] {
        constexpr M<Bytes, N> value(0xa55aa55aa55aa55aull);
        return ds::all_of(M<Bytes, N>(value.to_bitset()) == value);
    }());
#endif
}

template<int... I>
void check_widths(std::integer_sequence<int, I...>) {
    (check_width<1, I + 1>(), ...);
    (check_width<2, I + 1>(), ...);
    (check_width<4, I + 1>(), ...);
    (check_width<8, I + 1>(), ...);
    (check_width<16, I + 1>(), ...);
}

template<std::size_t Bytes, int N>
constexpr bool check_reduction_cases(std::uint64_t bits) {
    const M<Bytes, N> mask(bits);
    int count = 0, first = 0, last = 0;
    for (int i = 0; i < N; ++i) {
        const bool selected = ((bits >> i) & 1u) != 0;
        if (selected) { if (count == 0) first = i; last = i; ++count; }
    }
    // Index reductions require at least one selected lane.
    return ds::all_of(mask) == (count == N) && ds::any_of(mask) == (count != 0) &&
        ds::none_of(mask) == (count == 0) && ds::reduce_count(mask) == count &&
        (count == 0 || (ds::reduce_min_index(mask) == first && ds::reduce_max_index(mask) == last));
}

template<std::size_t Bytes, int N>
void check_runtime_reductions() {
    // Separate evaluations keep the compiler's per-expression step budget small.
    static_assert(check_reduction_cases<Bytes, N>(0));
    static_assert(check_reduction_cases<Bytes, N>(~std::uint64_t{0}));
    static_assert(check_reduction_cases<Bytes, N>(1));
    static_assert(check_reduction_cases<Bytes, N>(std::uint64_t{1} << (N / 2)));
    static_assert(check_reduction_cases<Bytes, N>(0x5555555555555555ull));
    static_assert(check_reduction_cases<Bytes, N>(0xaaaaaaaaaaaaaaaaull));
    static_assert(check_reduction_cases<Bytes, N>(std::uint64_t{1} << (N - 1)));
    static_assert(check_reduction_cases<Bytes, N>(~std::uint64_t{1}));
    static_assert(check_reduction_cases<Bytes, N>(~(std::uint64_t{1} << (N - 1))));
    // Volatile input forces runtime evaluation of the reduction loops.
    volatile std::uint64_t input = 0;
    EXPECT_TRUE((check_reduction_cases<Bytes, N>(input)));
    input = ~std::uint64_t{0};
    EXPECT_TRUE((check_reduction_cases<Bytes, N>(input)));
    input = 0x5555555555555555ull;
    EXPECT_TRUE((check_reduction_cases<Bytes, N>(input)));
    input = 0xaaaaaaaaaaaaaaaaull;
    EXPECT_TRUE((check_reduction_cases<Bytes, N>(input)));
    for (int i = 0; i < N; ++i) {
        input = std::uint64_t{1} << i;
        EXPECT_TRUE((check_reduction_cases<Bytes, N>(input)));
        input = ~(std::uint64_t{1} << i);
        EXPECT_TRUE((check_reduction_cases<Bytes, N>(input)));
    }
}

template<int... N>
void check_reduction_widths(std::integer_sequence<int, N...>) {
    (check_runtime_reductions<1, N>(), ...);
    (check_runtime_reductions<2, N>(), ...);
    (check_runtime_reductions<4, N>(), ...);
    (check_runtime_reductions<8, N>(), ...);
    (check_runtime_reductions<16, N>(), ...);
}

TEST(SimdMaskInteger, ReductionLanePatterns) {
    check_reduction_widths(std::integer_sequence<int, 1, 3, 4, 5, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64>{});
}

TEST(SimdMaskInteger, AllWidthsAndRepresentations) {
    check_widths(std::make_integer_sequence<int, 64>{});
}

// select and masked integer sums use blend, xor-blend, in-place and
// branch-free forms depending on the width and the compiler. Every form must
// keep the chosen lanes bit for bit and match the lane-by-lane result.
template<class T>
constexpr bool same_bits(const T& left, const T& right) {
    if constexpr (std::is_arithmetic_v<T>) {
        using Raw = ds::__detail::mask_lane_t<sizeof(T)>;
        return std::bit_cast<Raw>(left) == std::bit_cast<Raw>(right);
    } else {
        return same_bits(left.real(), right.real()) && same_bits(left.imag(), right.imag());
    }
}

// Special floating values (a NaN payload, signed zeros, infinities and a
// subnormal) appear only at runtime; constant evaluation uses finite values.
template<class T>
constexpr T select_sample(int i, bool second, bool special) {
    if constexpr (std::is_floating_point_v<T>) {
        if (special) {
            using Raw = ds::__detail::mask_lane_t<sizeof(T)>;
            const Raw quiet = std::bit_cast<Raw>(std::numeric_limits<T>::quiet_NaN());
            const T values[]{std::bit_cast<T>(static_cast<Raw>(quiet | Raw{5})), -T(0), T(0),
                std::numeric_limits<T>::infinity(), -std::numeric_limits<T>::infinity(),
                std::numeric_limits<T>::denorm_min(), T(1.5), T(-2.25)};
            return values[(2 * i + (second ? 1 : 0)) % 8];
        }
        const T values[]{T(0.5), -T(0), T(0), T(3), T(-4), T(1.5), T(-2.25), T(8)};
        return values[(2 * i + (second ? 1 : 0)) % 8];
    } else if constexpr (std::is_arithmetic_v<T>) {
        const T values[]{std::numeric_limits<T>::lowest(), std::numeric_limits<T>::max(), T(0), T(1),
            static_cast<T>(-1), T(42), static_cast<T>(100 + i), T(7)};
        return values[(3 * i + (second ? 5 : 0)) % 8];
    } else {
        using R = typename T::value_type;
        return T(select_sample<R>(i, second, special), select_sample<R>(i + 1, !second, special));
    }
}

template<class T, int N>
constexpr bool check_select_bits(std::uint64_t bits, bool special) {
    using V = ds::basic_vec<T, ds::__fixed_size_abi<N>>;
    const V when_true([special](auto i) { return select_sample<T>(int(i), false, special); });
    const V when_false([special](auto i) { return select_sample<T>(int(i), true, special); });
    const typename V::mask_type mask([bits](auto i) { return ((bits >> int(i)) & 1u) != 0; });
    const V result = ds::select(mask, when_true, when_false);
    for (int i = 0; i < N; ++i) {
        const T expected = ((bits >> i) & 1u) != 0 ? when_true[i] : when_false[i];
        if (!same_bits(result[i], expected)) return false;
    }
    return true;
}

template<class T, int N>
void check_select_width() {
    SCOPED_TRACE(N);
    if constexpr (N <= 8) {
        for (std::uint64_t bits = 0; bits < (std::uint64_t{1} << N); ++bits)
            EXPECT_TRUE((check_select_bits<T, N>(bits, true)));
    } else {
        for (const std::uint64_t bits : {std::uint64_t{0}, ~std::uint64_t{0}, std::uint64_t{0x5555555555555555},
                 std::uint64_t{0xaaaaaaaaaaaaaaaa}, std::uint64_t{0x0123456789abcdef}})
            EXPECT_TRUE((check_select_bits<T, N>(bits, true)));
        for (int i = 0; i < N; ++i)
            EXPECT_TRUE((check_select_bits<T, N>(std::uint64_t{1} << i, true)));
    }
}

template<int... N>
void check_select_widths(std::integer_sequence<int, N...>) {
    (check_select_width<float, N>(), ...);
    (check_select_width<double, N>(), ...);
    (check_select_width<std::int8_t, N>(), ...);
    (check_select_width<std::int32_t, N>(), ...);
    (check_select_width<std::int64_t, N>(), ...);
    (check_select_width<std::uint64_t, N>(), ...);
}

TEST(SimdMaskInteger, SelectKeepsChosenBits) {
    check_select_widths(std::integer_sequence<int, 1, 2, 3, 4, 8, 9, 16, 17, 32>{});
    check_select_width<std::complex<float>, 2>();
    check_select_width<std::complex<float>, 3>();
    static_assert(check_select_bits<double, 2>(0b01, false));
    static_assert(check_select_bits<std::int64_t, 2>(0b10, false));
    static_assert(check_select_bits<std::complex<float>, 2>(0b01, false));
    static_assert(check_select_bits<float, 16>(0xa55a, false));
    static_assert(check_select_bits<double, 16>(0x0ff0, false));
    static_assert(check_select_bits<std::int8_t, 32>(0x0123abcd, false));
}

// Narrow and unsigned lanes exercise wrap-around sums, which are defined for
// them; wider signed lanes stay small so that no sum overflows.
template<class T>
constexpr T plus_sample(int i) {
    if constexpr (std::is_unsigned_v<T>) {
        return static_cast<T>(std::numeric_limits<T>::max() - static_cast<T>(3 * i));
    } else if constexpr (sizeof(T) <= 2) {
        return static_cast<T>(100 + 13 * i);
    } else {
        return static_cast<T>((i % 3 - 1) * (1 << 20) + i);
    }
}

template<class T, int N>
constexpr bool check_masked_plus(std::uint64_t bits) {
    using V = ds::basic_vec<T, ds::__fixed_size_abi<N>>;
    using U = std::make_unsigned_t<T>;
    const V values([](auto i) { return plus_sample<T>(int(i)); });
    const typename V::mask_type mask([bits](auto i) { return ((bits >> int(i)) & 1u) != 0; });
    U sum = 0;
    for (int i = 0; i < N; ++i)
        if (((bits >> i) & 1u) != 0) sum = static_cast<U>(sum + static_cast<U>(values[i]));
    // An empty mask yields the identity, T() for plus.
    const T expected = static_cast<T>(sum);
    return ds::reduce(values, mask) == expected && ds::reduce(values, mask, std::plus<>{}) == expected &&
        ds::reduce(values, mask, std::plus<>{}, T(0)) == expected;
}

template<class T, int N>
void check_masked_plus_width() {
    SCOPED_TRACE(N);
    if constexpr (N <= 8) {
        for (std::uint64_t bits = 0; bits < (std::uint64_t{1} << N); ++bits)
            EXPECT_TRUE((check_masked_plus<T, N>(bits)));
    } else {
        for (const std::uint64_t bits : {std::uint64_t{0}, ~std::uint64_t{0}, std::uint64_t{0x5555555555555555},
                 std::uint64_t{0xaaaaaaaaaaaaaaaa}, std::uint64_t{0x0123456789abcdef}})
            EXPECT_TRUE((check_masked_plus<T, N>(bits)));
        for (int i = 0; i < N; ++i) {
            EXPECT_TRUE((check_masked_plus<T, N>(std::uint64_t{1} << i)));
            EXPECT_TRUE((check_masked_plus<T, N>(~(std::uint64_t{1} << i))));
        }
    }
}

template<int... N>
void check_masked_plus_widths(std::integer_sequence<int, N...>) {
    (check_masked_plus_width<std::int8_t, N>(), ...);
    (check_masked_plus_width<std::uint8_t, N>(), ...);
    (check_masked_plus_width<std::int16_t, N>(), ...);
    (check_masked_plus_width<std::int32_t, N>(), ...);
    (check_masked_plus_width<std::uint32_t, N>(), ...);
    (check_masked_plus_width<std::int64_t, N>(), ...);
    (check_masked_plus_width<std::uint64_t, N>(), ...);
}

TEST(SimdMaskInteger, MaskedIntegerPlus) {
    check_masked_plus_widths(std::integer_sequence<int, 1, 2, 3, 4, 5, 8, 12, 16, 17, 32>{});
    static_assert(check_masked_plus<std::int32_t, 8>(0xa5));
    static_assert(check_masked_plus<std::int32_t, 4>(0));
    static_assert(check_masked_plus<std::uint8_t, 16>(0xffff));
    static_assert(check_masked_plus<std::int16_t, 8>(0x3c));
    static_assert(check_masked_plus<std::uint64_t, 2>(0b11));
}

// big + 1 rounds back to big while 1 + 1 + big does not, so the bits show the
// order and the number of additions; a lone -0.0 shows that no zero is added.
template<class T>
constexpr T order_sample(int i) {
    constexpr T big = std::is_same_v<T, float> ? T(16777216.0) : T(9007199254740992.0);
    return i % 3 == 0 ? T(1) : (i % 3 == 1 ? big : -T(0));
}

template<class T, int N>
constexpr bool check_masked_order(std::uint64_t bits) {
    using V = ds::basic_vec<T, ds::__fixed_size_abi<N>>;
    const V values([](auto i) { return order_sample<T>(int(i)); });
    const typename V::mask_type mask([bits](auto i) { return ((bits >> int(i)) & 1u) != 0; });
    // The first selected lane starts the sum; each later one is added in lane order.
    const T identity = T(7);
    T expected = identity;
    bool started = false;
    for (int i = 0; i < N; ++i) {
        if (((bits >> i) & 1u) == 0) continue;
        expected = started ? T(expected + values[i]) : values[i];
        started = true;
    }
    return same_bits(ds::reduce(values, mask, std::plus<>{}, identity), expected) &&
        same_bits(ds::reduce(values, mask), started ? expected : T());
}

template<class T, int N>
void check_masked_order_width() {
    SCOPED_TRACE(N);
    if constexpr (N <= 8) {
        for (std::uint64_t bits = 0; bits < (std::uint64_t{1} << N); ++bits)
            EXPECT_TRUE((check_masked_order<T, N>(bits)));
    } else {
        for (const std::uint64_t bits : {std::uint64_t{0}, ~std::uint64_t{0}, std::uint64_t{0x5555555555555555},
                 std::uint64_t{0xaaaaaaaaaaaaaaaa}, std::uint64_t{0x0123456789abcdef}})
            EXPECT_TRUE((check_masked_order<T, N>(bits)));
        for (int i = 0; i < N; ++i) {
            EXPECT_TRUE((check_masked_order<T, N>(std::uint64_t{1} << i)));
            EXPECT_TRUE((check_masked_order<T, N>(~(std::uint64_t{1} << i))));
        }
    }
}

template<int... N>
void check_masked_order_widths(std::integer_sequence<int, N...>) {
    (check_masked_order_width<float, N>(), ...);
    (check_masked_order_width<double, N>(), ...);
}

TEST(SimdMaskInteger, MaskedFloatingReduceOrder) {
    check_masked_order_widths(std::integer_sequence<int, 1, 2, 3, 4, 5, 8, 9, 16, 17, 32>{});
    static_assert(check_masked_order<float, 8>(0xa5));
    static_assert(check_masked_order<double, 4>(0b0110));
    static_assert(check_masked_order<float, 16>(0x8421));
    static_assert(check_masked_order<double, 3>(0b100));
    static_assert(check_masked_order<float, 1>(0));
}
} // namespace
