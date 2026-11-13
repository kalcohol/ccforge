#include <forge/simd.hpp>
#include <type_traits>
#include <utility>

using V = std::simd::vec<int>;
using V4 = std::simd::vec<int, 4>;
static_assert(std::is_same_v<forge::simd_native_abi<int>, typename V::abi_type>);
static_assert(std::is_same_v<forge::simd_abi<int, 4>, typename V4::abi_type>);
static_assert(std::is_same_v<std::simd::basic_vec<int, forge::simd_abi<int, 4>>, V4>);
static_assert(std::is_same_v<typename std::simd::mask<int, 4>::abi_type, forge::simd_abi<int, 4>>);
using Resized = std::simd::resize_t<4, V>;
using Rebound = std::simd::rebind_t<int, V4>;
static_assert(std::is_same_v<typename Resized::value_type, int>);
static_assert(Resized::size() == 4);
static_assert(std::is_same_v<typename Rebound::value_type, int>);
static_assert(Rebound::size() == V4::size());
#if defined(FORGE_BACKPORT_SIMD_HPP_INCLUDED)
// The backport makes a stronger ABI choice than the standard requires.
static_assert(std::is_same_v<typename std::simd::resize_t<4, V>::abi_type, forge::simd_abi<int, 4>>);
static_assert(std::is_same_v<std::simd::rebind_t<int, V4>, V4>);
static_assert(std::is_same_v<forge::simd_fixed_size_abi<4>, std::simd::__fixed_size_abi<4>>);
static_assert(std::is_same_v<forge::simd_native_abi_tag<int, 4>, std::simd::__detail::__native_abi_tag<int, 4>>);
static_assert(std::simd::basic_vec<int, forge::simd_fixed_size_abi<4>>::size() == 4);
static_assert(std::simd::basic_vec<int, forge::simd_native_abi_tag<int, 4>>::size() == 4);
#endif
int main() {}
