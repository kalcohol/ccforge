#include "simd_test_common.hpp"
#include <gtest/gtest.h>
#include <cstdint>
#include <limits>

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
} // namespace
