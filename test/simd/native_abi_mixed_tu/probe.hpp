#pragma once

#include <simd>

#include <cstddef>
#include <type_traits>
#include <typeinfo>
#include <utility>

#ifndef FORGE_BACKPORT_SIMD_HPP_INCLUDED
#error "The native ABI fixture must exercise the Forge backport"
#endif

namespace native_abi_fixture {

using narrow_tag = std::simd::detail::native_abi_tag<int, 4>;
using wide_tag = std::simd::detail::native_abi_tag<int, 8>;
using narrow_vec = std::simd::basic_vec<int, narrow_tag>;
using wide_vec = std::simd::basic_vec<int, wide_tag>;
using narrow_float_tag = std::simd::detail::native_abi_tag<float, 4>;
using wide_float_tag = std::simd::detail::native_abi_tag<float, 8>;
using narrow_float_vec = std::simd::basic_vec<float, narrow_float_tag>;
using wide_float_vec = std::simd::basic_vec<float, wide_float_tag>;

static_assert(sizeof(int) == 4);
static_assert(narrow_vec::size() == 4 && wide_vec::size() == 8);
static_assert(sizeof(narrow_vec) == 16 && sizeof(wide_vec) == 32);
static_assert(narrow_vec::mask_type::size() == 4 && wide_vec::mask_type::size() == 8);
static_assert(std::simd::simd_size<float, narrow_tag>::value == 4);
static_assert(std::simd::simd_size<float, wide_tag>::value == 8);
static_assert(std::simd::alignment_v<narrow_vec> == alignof(int) * 4);
static_assert(std::simd::alignment_v<wide_vec> == alignof(int) * 8);

template<class V, class Abi, std::simd::simd_size_type N>
constexpr bool check_float_traits() {
    using mask_type = typename V::mask_type;
    static_assert(std::is_same_v<typename V::value_type, float>);
    static_assert(std::is_same_v<typename V::abi_type, Abi>);
    static_assert(std::is_same_v<mask_type, std::simd::basic_mask<sizeof(float), Abi>>);
    static_assert(std::is_same_v<typename mask_type::value_type, bool>);
    static_assert(std::is_same_v<typename mask_type::abi_type, Abi>);
    static_assert(std::is_default_constructible_v<V> && std::is_trivially_copyable_v<V>);
    static_assert(std::is_default_constructible_v<mask_type> && std::is_trivially_copyable_v<mask_type>);
    static_assert(V::size() == N && mask_type::size() == N);
    static_assert(std::simd::simd_size<float, Abi>::value == N);
    static_assert(std::simd::abi_lane_count<Abi>::value == N);
    static_assert(sizeof(V) == sizeof(float) * N && alignof(V) == alignof(float));
    static_assert(sizeof(mask_type) == sizeof(bool) * N && alignof(mask_type) == alignof(bool));
    static_assert(std::simd::alignment_v<V> == alignof(float) * N);
    static_assert(std::simd::alignment_v<V, int> == alignof(int) * N);
    static_assert(std::simd::rebind_t<int, V>::size() == N);
    static_assert(std::simd::rebind_t<int, mask_type>::size() == N);
    static_assert(std::simd::resize_t<4, V>::size() == 4);
    static_assert(std::simd::resize_t<8, V>::size() == 8);
    static_assert(std::simd::resize_t<4, mask_type>::size() == 4);
    static_assert(std::simd::resize_t<8, mask_type>::size() == 8);
    static_assert(std::is_same_v<decltype(std::simd::sqrt(std::declval<const V&>())), V>);
    static_assert(std::is_same_v<decltype(std::simd::abs(std::declval<const V&>())), V>);
    static_assert(std::is_same_v<decltype(std::sqrt(std::declval<const V&>())), V>);
    static_assert(std::is_same_v<decltype(std::abs(std::declval<const V&>())), V>);
    static_assert(std::is_same_v<decltype(std::simd::isfinite(std::declval<const V&>())), mask_type>);
    return true;
}

static_assert(check_float_traits<narrow_float_vec, narrow_float_tag, 4>());
static_assert(check_float_traits<wide_float_vec, wide_float_tag, 8>());

struct snapshot {
    const std::type_info* abi_type;
    const std::type_info* vec_type;
    const std::type_info* mask_type;
    const std::type_info* narrow_float_type;
    const std::type_info* wide_float_type;
    std::size_t lanes;
    std::size_t vec_bytes;
    std::size_t mask_bytes;
    bool values_ok;
    bool narrow_float_values_ok;
    bool wide_float_values_ok;
};

snapshot probe_first() noexcept;
snapshot probe_second() noexcept;
narrow_vec roundtrip_narrow_first(narrow_vec value) noexcept;
narrow_vec roundtrip_narrow_second(narrow_vec value) noexcept;
wide_vec roundtrip_wide_first(wide_vec value) noexcept;
wide_vec roundtrip_wide_second(wide_vec value) noexcept;
narrow_float_vec roundtrip_float_narrow_first(narrow_float_vec value) noexcept;
narrow_float_vec roundtrip_float_narrow_second(narrow_float_vec value) noexcept;
wide_float_vec roundtrip_float_wide_first(wide_float_vec value) noexcept;
wide_float_vec roundtrip_float_wide_second(wide_float_vec value) noexcept;

} // namespace native_abi_fixture
