#include <forge/simd.hpp>
#include <complex>
#include <type_traits>
#include <utility>

// Suppress only compatibility uses; headers are checked with warnings as errors.
#ifndef FORGE_SIMD_DEPRECATION_CASE
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4996)
#elif defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
#endif

#if !defined(FORGE_SIMD_DEPRECATION_CASE) || FORGE_SIMD_DEPRECATION_CASE == 1
namespace compatibility_1 {
using old_type = std::simd::simd_size_type;
static_assert(std::is_same_v<old_type, std::simd::__simd_size_type>);
}
#endif

#if !defined(FORGE_SIMD_DEPRECATION_CASE) || FORGE_SIMD_DEPRECATION_CASE == 2
namespace compatibility_2 {
using old_type = std::simd::fixed_size_abi<4>;
static_assert(std::is_same_v<old_type, forge::simd_fixed_size_abi<4>>);
}
#endif

#if !defined(FORGE_SIMD_DEPRECATION_CASE) || FORGE_SIMD_DEPRECATION_CASE == 3
namespace compatibility_3 {
using old_type = std::simd::native_abi<int>;
static_assert(std::is_same_v<old_type, forge::simd_native_abi<int>>);
}
#endif

#if !defined(FORGE_SIMD_DEPRECATION_CASE) || FORGE_SIMD_DEPRECATION_CASE == 4
namespace compatibility_4 {
using old_type = std::simd::simd_size<int, forge::simd_fixed_size_abi<4>>;
static_assert(old_type::value == 4);
static_assert(std::is_same_v<old_type, std::simd::__simd_size<int, forge::simd_fixed_size_abi<4>>>);
}
#endif

#if !defined(FORGE_SIMD_DEPRECATION_CASE) || FORGE_SIMD_DEPRECATION_CASE == 5
namespace compatibility_5 {
using old_type = std::simd::abi_lane_count<forge::simd_fixed_size_abi<4>>;
static_assert(old_type::value == 4);
static_assert(std::is_same_v<old_type, std::simd::__abi_lane_count<forge::simd_fixed_size_abi<4>>>);
}
#endif

#if !defined(FORGE_SIMD_DEPRECATION_CASE) || FORGE_SIMD_DEPRECATION_CASE == 6
namespace compatibility_6 {
using old_type = std::simd::deduce_abi_t<int, 4>;
static_assert(std::is_same_v<old_type, forge::simd_abi<int, 4>>);
}
#endif

#if !defined(FORGE_SIMD_DEPRECATION_CASE) || FORGE_SIMD_DEPRECATION_CASE == 7
namespace compatibility_7 {
using old_type = std::simd::simd_iterator<std::simd::vec<int, 4>>;
static_assert(std::is_same_v<old_type, decltype(std::declval<std::simd::vec<int, 4>&>().begin())>);
}
#endif

#if !defined(FORGE_SIMD_DEPRECATION_CASE) || FORGE_SIMD_DEPRECATION_CASE == 8
namespace compatibility_8 {
using old_type = std::simd::convert_flag;
static_assert(std::is_same_v<std::simd::flags<old_type>, std::remove_cv_t<decltype(std::simd::flag_convert)>>);
}
#endif

#if !defined(FORGE_SIMD_DEPRECATION_CASE) || FORGE_SIMD_DEPRECATION_CASE == 9
namespace compatibility_9 {
using old_type = std::simd::aligned_flag;
static_assert(std::is_same_v<std::simd::flags<old_type>, std::remove_cv_t<decltype(std::simd::flag_aligned)>>);
}
#endif

#if !defined(FORGE_SIMD_DEPRECATION_CASE) || FORGE_SIMD_DEPRECATION_CASE == 10
namespace compatibility_10 {
using old_type = std::simd::overaligned_flag<16>;
static_assert(std::is_same_v<std::simd::flags<old_type>, std::remove_cv_t<decltype(std::simd::flag_overaligned<16>)>>);
}
#endif

#if !defined(FORGE_SIMD_DEPRECATION_CASE) || FORGE_SIMD_DEPRECATION_CASE == 11
namespace compatibility_11 {
using old_type = std::simd::real_simd_type<std::complex<float>, std::simd::vec<std::complex<float>, 4>>;
static_assert(std::is_same_v<old_type, std::simd::__real_simd_type<std::complex<float>, std::simd::vec<std::complex<float>, 4>>>);
static_assert(std::is_same_v<typename old_type::type, std::simd::rebind_t<float, std::simd::vec<std::complex<float>, 4>>>);
}
#endif

#if !defined(FORGE_SIMD_DEPRECATION_CASE) || FORGE_SIMD_DEPRECATION_CASE == 12
namespace compatibility_12 {
using old_type = std::simd::real_simd_t<std::complex<float>, std::simd::vec<std::complex<float>, 4>>;
static_assert(std::is_same_v<old_type, std::simd::rebind_t<float, std::simd::vec<std::complex<float>, 4>>>);
}
#endif

#if !defined(FORGE_SIMD_DEPRECATION_CASE) || FORGE_SIMD_DEPRECATION_CASE == 13
namespace compatibility_13 {
using old_type = std::simd::detail::native_abi_tag<int, 4>;
static_assert(std::is_same_v<old_type, forge::simd_native_abi_tag<int, 4>>);
}
#endif

#if !defined(FORGE_SIMD_DEPRECATION_CASE) || FORGE_SIMD_DEPRECATION_CASE == 14
namespace compatibility_14 {
using V = std::simd::vec<std::complex<float>, 4>;
static_assert(std::is_same_v<typename V::real_type, std::simd::rebind_t<float, V>>);
}
#endif
#if !defined(FORGE_SIMD_DEPRECATION_CASE) || FORGE_SIMD_DEPRECATION_CASE == 15
namespace compatibility_15 {
using V = std::simd::vec<int, 4>;
constexpr auto value = simd_select_impl(typename V::mask_type(true), V(7), V(9));
static_assert(value[0] == 7 && value[3] == 7);
static_assert(noexcept(simd_select_impl(typename V::mask_type(true), V(7), V(9))));
using M = typename V::mask_type;
constexpr auto vector_brace = simd_select_impl(M(true), V(7), {});
constexpr auto mask_brace = simd_select_impl(M(true), M(true), {});
constexpr auto condition_brace = simd_select_impl({}, V(7), V(9));
constexpr auto scalar_mask = simd_select_impl(M(true), true, false);
constexpr auto scalar_vector = simd_select_impl(M(true), 7, 9);
static_assert(vector_brace[0] == 7 && mask_brace[0]);
static_assert(condition_brace[0] == 9 && scalar_mask[0] && scalar_vector[0] == 7);
static_assert(noexcept(simd_select_impl(M(true), V(7), {})));
static_assert(noexcept(simd_select_impl(M(true), M(true), {})));
}
#endif

#ifndef FORGE_SIMD_DEPRECATION_CASE
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
#endif

int main() {}
