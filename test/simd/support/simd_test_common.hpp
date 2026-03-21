#pragma once

#include <simd>

#include <array>
#include <bitset>
#include <complex>
#include <deque>
#include <iterator>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace simd_test {

using int4 = std::simd::vec<int, 4>;
using int3 = std::simd::vec<int, 3>;
using int2 = std::simd::vec<int, 2>;
using int1 = std::simd::vec<int, 1>;
using int8 = std::simd::resize_t<8, int4>;
using default_int = std::simd::vec<int>;
using float4 = std::simd::rebind_t<float, int4>;
using double4 = std::simd::vec<double, 4>;
using long4 = std::simd::vec<long, 4>;
using longlong4 = std::simd::vec<long long, 4>;
using uint4 = std::simd::vec<unsigned, 4>;
using ulong4 = std::simd::vec<unsigned long, 4>;
using uchar4 = std::simd::vec<unsigned char, 4>;
using schar4 = std::simd::vec<signed char, 4>;
using mask4 = std::simd::mask<int, 4>;
using byte_mask4 = std::simd::mask<signed char, 4>;
using uint_mask4 = typename uint4::mask_type;
using uchar_mask4 = typename uchar4::mask_type;
using complex4f = std::simd::vec<std::complex<float>, 4>;
using complex4d = std::simd::vec<std::complex<double>, 4>;
using default_complexf = std::simd::basic_vec<std::complex<float>>;
using default_complexd = std::simd::basic_vec<std::complex<double>>;
#if defined(__SIZEOF_INT128__)
using int128 = __int128;
using uint128 = unsigned __int128;
using default_int128 = std::simd::basic_vec<int128>;
using default_uint128 = std::simd::basic_vec<uint128>;
#endif
using int_iter = std::vector<int>::iterator;
using const_int_iter = std::vector<int>::const_iterator;
using deque_int_iter = std::deque<int>::iterator;

template<class V>
constexpr auto lane(const V& value, std::simd::simd_size_type index) -> decltype(value[index]) {
    return value[index];
}

template<class V, class T, size_t N>
V load_vec(const std::array<T, N>& values) {
    return std::simd::partial_load<V>(values.data(), static_cast<std::simd::simd_size_type>(N));
}

struct fake_index_vector {
    using value_type = int;
    static constexpr std::simd::simd_size_type size = 4;

    int operator[](std::simd::simd_size_type) const noexcept;
};

struct int_generator {
    template<class Index>
    constexpr int operator()(Index) const noexcept {
        return static_cast<int>(Index::value * 2 + 1);
    }
};

struct bool_generator {
    template<class Index>
    constexpr bool operator()(Index) const noexcept {
        return (Index::value % 2) == 0;
    }
};

struct int_returning_generator {
    template<class Index>
    constexpr int operator()(Index) const noexcept {
        return 0;
    }
};

struct explicit_to_int {
    explicit constexpr operator int() const noexcept {
        return 7;
    }
};

struct wrapper_bad_value {
    static constexpr int value = 7;

    constexpr operator int() const noexcept {
        return 6;
    }
};

template<class V, class Indices, class = void>
struct has_dynamic_permute : std::false_type {};

template<class V, class Indices>
struct has_dynamic_permute<V,
                           Indices,
                           std::void_t<decltype(std::simd::permute(std::declval<const V&>(),
                                                                   std::declval<const Indices&>()))>>
    : std::true_type {};

template<class V, class Indices, class = void>
struct has_dynamic_subscript : std::false_type {};

template<class V, class Indices>
struct has_dynamic_subscript<V,
                             Indices,
                             std::void_t<decltype(std::declval<const V&>()[std::declval<const Indices&>()])>>
    : std::true_type {};

template<class V, class I, class = void>
struct has_partial_load_count : std::false_type {};

template<class V, class I>
struct has_partial_load_count<V,
                              I,
                              std::void_t<decltype(std::simd::partial_load<V>(std::declval<I>(),
                                                                             std::simd::simd_size_type{}))>>
    : std::true_type {};

template<class V, class I, class = void>
struct has_partial_store_count : std::false_type {};

template<class V, class I>
struct has_partial_store_count<V,
                               I,
                               std::void_t<decltype(std::simd::partial_store(std::declval<const V&>(),
                                                                             std::declval<I>(),
                                                                             std::simd::simd_size_type{}))>>
    : std::true_type {};

template<class V, class R, class = void>
struct has_partial_load_range : std::false_type {};

template<class V, class R>
struct has_partial_load_range<V, R, std::void_t<decltype(std::simd::partial_load<V>(std::declval<R>()))>>
    : std::true_type {};

template<class V, class R, class = void>
struct has_partial_store_range : std::false_type {};

template<class V, class R>
struct has_partial_store_range<V,
                               R,
                               std::void_t<decltype(std::simd::partial_store(std::declval<const V&>(),
                                                                             std::declval<R>()))>>
    : std::true_type {};

template<class V, class R, class Indices, class = void>
struct has_partial_gather_range : std::false_type {};

template<class V, class R, class Indices>
struct has_partial_gather_range<V,
                                R,
                                Indices,
                                std::void_t<decltype(std::simd::partial_gather_from<V>(
                                    std::declval<R>(),
                                    std::declval<const Indices&>()))>> : std::true_type {};

template<class V, class R, class Indices, class = void>
struct has_partial_scatter_range : std::false_type {};

template<class V, class R, class Indices>
struct has_partial_scatter_range<V,
                                 R,
                                 Indices,
                                 std::void_t<decltype(std::simd::partial_scatter_to(std::declval<const V&>(),
                                                                                    std::declval<R>(),
                                                                                    std::declval<const Indices&>()))>>
    : std::true_type {};

} // namespace simd_test
