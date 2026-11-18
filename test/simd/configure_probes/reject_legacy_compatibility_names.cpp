#include <forge/simd.hpp>

#include <complex>
#include <type_traits>
#include <utility>

using int4 = std::simd::vec<int, 4>;
using mask4 = int4::mask_type;
using complex4 = std::simd::vec<std::complex<float>, 4>;

// A restored deprecated declaration must pass lookup even in a strict build.
#if !defined(FORGE_SIMD_POSITIVE_CONTROL)
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4996)
#endif
#endif

#if defined(FORGE_SIMD_LEGACY_SIMD_SIZE_TYPE)
#if defined(FORGE_SIMD_POSITIVE_CONTROL)
using size_type = decltype(int4::size());
#else
using size_type = std::simd::simd_size_type;
#endif
#elif defined(FORGE_SIMD_LEGACY_FIXED_SIZE_ABI)
#if defined(FORGE_SIMD_POSITIVE_CONTROL)
using abi = std::simd::vec<int, 4>::abi_type;
#else
using abi = std::simd::fixed_size_abi<4>;
#endif
#elif defined(FORGE_SIMD_LEGACY_NATIVE_ABI)
#if defined(FORGE_SIMD_POSITIVE_CONTROL)
using abi = std::simd::vec<int>::abi_type;
#else
using abi = std::simd::native_abi<int>;
#endif
#elif defined(FORGE_SIMD_LEGACY_SIMD_SIZE)
#if defined(FORGE_SIMD_POSITIVE_CONTROL)
static_assert(std::simd::basic_vec<int, int4::abi_type>::size() == 4);
#else
using size = std::simd::simd_size<int, int4::abi_type>;
#endif
#elif defined(FORGE_SIMD_LEGACY_ABI_LANE_COUNT)
#if defined(FORGE_SIMD_POSITIVE_CONTROL)
static_assert(mask4::size() == 4);
#else
using size = std::simd::abi_lane_count<mask4::abi_type>;
#endif
#elif defined(FORGE_SIMD_LEGACY_DEDUCE_ABI_T)
#if defined(FORGE_SIMD_POSITIVE_CONTROL)
using abi = std::simd::vec<int, 4>::abi_type;
#else
using abi = std::simd::deduce_abi_t<int, 4>;
#endif
#elif defined(FORGE_SIMD_LEGACY_SIMD_ITERATOR)
#if defined(FORGE_SIMD_POSITIVE_CONTROL)
using iterator = decltype(std::declval<int4&>().begin());
#else
using iterator = std::simd::simd_iterator<int4>;
#endif
#elif defined(FORGE_SIMD_LEGACY_CONVERT_FLAG)
#if defined(FORGE_SIMD_POSITIVE_CONTROL)
using flag = std::remove_cv_t<decltype(std::simd::flag_convert)>;
#else
using flag = std::simd::convert_flag;
#endif
#elif defined(FORGE_SIMD_LEGACY_ALIGNED_FLAG)
#if defined(FORGE_SIMD_POSITIVE_CONTROL)
using flag = std::remove_cv_t<decltype(std::simd::flag_aligned)>;
#else
using flag = std::simd::aligned_flag;
#endif
#elif defined(FORGE_SIMD_LEGACY_OVERALIGNED_FLAG)
#if defined(FORGE_SIMD_POSITIVE_CONTROL)
using flag = std::remove_cv_t<decltype(std::simd::flag_overaligned<16>)>;
#else
using flag = std::simd::overaligned_flag<16>;
#endif
#elif defined(FORGE_SIMD_LEGACY_REAL_SIMD_TYPE)
#if defined(FORGE_SIMD_POSITIVE_CONTROL)
using real_trait = std::simd::rebind<float, complex4>;
static_assert(std::is_same_v<real_trait::type, std::simd::rebind_t<float, complex4>>);
#else
using real_trait = std::simd::real_simd_type<std::complex<float>, complex4>;
#endif
#elif defined(FORGE_SIMD_LEGACY_REAL_SIMD_T)
#if defined(FORGE_SIMD_POSITIVE_CONTROL)
using real_vec = std::simd::rebind_t<float, complex4>;
#else
using real_vec = std::simd::real_simd_t<std::complex<float>, complex4>;
#endif
#elif defined(FORGE_SIMD_LEGACY_NATIVE_ABI_TAG)
#if defined(FORGE_SIMD_POSITIVE_CONTROL)
using abi = std::simd::vec<int, 4>::abi_type;
#else
using abi = std::simd::__detail::native_abi_tag<int, 4>;
#endif
#elif defined(FORGE_SIMD_LEGACY_DETAIL_NAMESPACE)
#if defined(FORGE_SIMD_POSITIVE_CONTROL)
namespace simd_api = std::simd;
static_assert(simd_api::vec<int, 4>::size() == 4);
#else
namespace simd_api = std::simd::detail;
#endif
#elif defined(FORGE_SIMD_LEGACY_SELECT_VEC)
#if defined(FORGE_SIMD_POSITIVE_CONTROL)
constexpr auto selected = std::simd::select(mask4(true), int4(7), int4(9));
static_assert(std::is_same_v<decltype(selected), const int4> && selected[0] == 7);
static_assert(noexcept(std::simd::select(mask4(true), int4(7), int4(9))));
#else
using selected = decltype(simd_select_impl(mask4(true), int4(7), int4(9)));
#endif
#elif defined(FORGE_SIMD_LEGACY_SELECT_MASK)
#if defined(FORGE_SIMD_POSITIVE_CONTROL)
constexpr auto selected = std::simd::select(mask4(true), mask4(true), mask4(false));
static_assert(std::is_same_v<decltype(selected), const mask4> && selected[0]);
static_assert(noexcept(std::simd::select(mask4(true), mask4(true), mask4(false))));
#else
using selected = decltype(simd_select_impl(mask4(true), mask4(true), mask4(false)));
#endif
#elif defined(FORGE_SIMD_LEGACY_SELECT_BOOL)
#if defined(FORGE_SIMD_POSITIVE_CONTROL)
constexpr auto selected = std::simd::select(mask4(false), true, false);
static_assert(std::is_same_v<decltype(selected), const mask4> && !selected[0]);
static_assert(noexcept(std::simd::select(mask4(false), true, false)));
#else
using selected = decltype(simd_select_impl(mask4(false), true, false));
#endif
#elif defined(FORGE_SIMD_LEGACY_SELECT_SCALAR)
#if defined(FORGE_SIMD_POSITIVE_CONTROL)
constexpr auto selected = std::simd::select(mask4(false), 7, 9);
static_assert(std::is_same_v<decltype(selected), const int4> && selected[0] == 9);
static_assert(noexcept(std::simd::select(mask4(false), 7, 9)));
#else
using selected = decltype(simd_select_impl(mask4(false), 7, 9));
#endif
#elif defined(FORGE_SIMD_LEGACY_REAL_TYPE)
#if defined(FORGE_SIMD_POSITIVE_CONTROL)
using real_vec = std::simd::rebind_t<float, complex4>;
#else
using real_vec = complex4::real_type;
#endif
#elif defined(FORGE_SIMD_LEGACY_FORGE_FIXED_SIZE_ABI)
#if defined(FORGE_SIMD_POSITIVE_CONTROL)
using abi = std::simd::vec<int, 4>::abi_type;
#else
using abi = forge::simd_fixed_size_abi<4>;
#endif
#elif defined(FORGE_SIMD_LEGACY_FORGE_NATIVE_ABI_TAG)
#if defined(FORGE_SIMD_POSITIVE_CONTROL)
using abi = std::simd::vec<int, 4>::abi_type;
#else
using abi = forge::simd_native_abi_tag<int, 4>;
#endif
#else
#error Missing legacy SIMD probe selector
#endif

#if !defined(FORGE_SIMD_POSITIVE_CONTROL)
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
#endif

int main() {}
