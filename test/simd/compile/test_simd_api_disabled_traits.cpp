#include <simd>

#include <complex>
#include <limits>
#include <type_traits>

#ifndef FORGE_BACKPORT_SIMD_HPP_INCLUDED
#error "This fixture checks the backport's disabled-source transformation traits."
#endif

namespace {

using size_type = std::simd::simd_size_type;

// [simd.general]/3 and [simd.traits]/3,7 require an enabled source.
// https://eel.is/c++draft/simd.traits
template<class T, class V>
concept has_rebind = requires { typename std::simd::rebind<T, V>::type; };

template<class T, class V>
concept has_rebind_alias = requires { typename std::simd::rebind_t<T, V>; };

template<size_type N, class V>
concept has_resize = requires { typename std::simd::resize<N, V>::type; };

template<size_type N, class V>
concept has_resize_alias = requires { typename std::simd::resize_t<N, V>; };

using disabled_value = std::simd::basic_vec<long double, std::simd::fixed_size_abi<2>>;
using disabled_width = std::simd::basic_vec<int, std::simd::fixed_size_abi<65>>;
using disabled_bytes = std::simd::basic_mask<3, std::simd::fixed_size_abi<4>>;
using disabled_mask_width = std::simd::basic_mask<sizeof(int), std::simd::fixed_size_abi<65>>;

// These four checks distinguish source eligibility independently of the
// earlier invalid-deduction naming repair and of transformed ABI identity.
static_assert(!has_rebind<int, disabled_value>);
static_assert(!has_rebind<int, disabled_bytes>);
static_assert(!has_resize<4, disabled_width>);
static_assert(!has_resize<4, disabled_mask_width>);

template<class V>
constexpr bool disabled_source_has_no_results() {
    return sizeof(std::simd::rebind<int, V>) > 0 &&
        sizeof(std::simd::resize<4, V>) > 0 &&
        !has_rebind<int, V> && !has_rebind<float, V> &&
        !has_rebind_alias<int, V> && !has_rebind_alias<float, V> &&
        !has_resize<1, V> && !has_resize<4, V> && !has_resize<64, V> &&
        !has_resize_alias<1, V> && !has_resize_alias<4, V> &&
        !has_resize_alias<64, V>;
}

static_assert(disabled_source_has_no_results<disabled_value>());
static_assert(disabled_source_has_no_results<disabled_width>());
static_assert(disabled_source_has_no_results<disabled_bytes>());
static_assert(disabled_source_has_no_results<disabled_mask_width>());

// An unrecognized ABI has no abi_lane_count; eligibility must short-circuit
// before querying it. Public disabled aliases exercise that path as well.
struct unknown_abi {};
using unknown_vector = std::simd::basic_vec<int, unknown_abi>;
using unknown_mask = std::simd::basic_mask<sizeof(int), unknown_abi>;
static_assert(disabled_source_has_no_results<unknown_vector>());
static_assert(disabled_source_has_no_results<unknown_mask>());
static_assert(disabled_source_has_no_results<std::simd::vec<long double, 2>>());
static_assert(disabled_source_has_no_results<std::simd::vec<int, 65>>());
static_assert(disabled_source_has_no_results<std::simd::mask<int, 65>>());
static_assert(disabled_source_has_no_results<std::simd::vec<int, 0>>());
static_assert(disabled_source_has_no_results<std::simd::mask<int, -1>>());

using vector4 = std::simd::vec<int, 4>;
using mask4 = typename vector4::mask_type;

template<class V>
constexpr bool invalid_targets_have_no_results() {
    return sizeof(std::simd::rebind<long double, V>) > 0 &&
        sizeof(std::simd::resize<0, V>) > 0 &&
        !has_rebind<long double, V> && !has_rebind<bool, V> &&
        !has_rebind<void, V> && !has_rebind<int*, V> &&
        !has_rebind_alias<long double, V> && !has_rebind_alias<bool, V> &&
        !has_rebind_alias<void, V> && !has_rebind_alias<int*, V> &&
        !has_resize<0, V> && !has_resize<-1, V> && !has_resize<65, V> &&
        !has_resize<std::numeric_limits<size_type>::max(), V> &&
        !has_resize_alias<0, V> && !has_resize_alias<-1, V> &&
        !has_resize_alias<65, V>;
}

static_assert(invalid_targets_have_no_results<vector4>());
static_assert(invalid_targets_have_no_results<mask4>());
static_assert(disabled_source_has_no_results<int>());

template<class V, class T, size_type N>
constexpr bool is_vector_result =
    std::is_same_v<V, std::simd::basic_vec<T, typename V::abi_type>> && V::size == N;

template<class V, class T, size_type N>
constexpr bool is_mask_result =
    std::is_same_v<V, std::simd::basic_mask<sizeof(T), typename V::abi_type>> && V::size == N;

// Enabled controls check value type/element size and width, not an ABI identity
// guarantee that rebind/resize are not required to provide.
static_assert(is_vector_result<std::simd::rebind_t<float, vector4>, float, 4>);
static_assert(is_vector_result<std::simd::rebind_t<std::complex<double>, vector4>,
    std::complex<double>, 4>);
static_assert(is_vector_result<std::simd::resize_t<1, vector4>, int, 1>);
static_assert(is_vector_result<std::simd::resize_t<8, vector4>, int, 8>);
static_assert(is_vector_result<std::simd::resize_t<64, vector4>, int, 64>);
static_assert(is_mask_result<std::simd::rebind_t<float, mask4>, float, 4>);
static_assert(is_mask_result<std::simd::rebind_t<double, mask4>, double, 4>);
static_assert(is_mask_result<std::simd::rebind_t<std::complex<double>, mask4>,
    std::complex<double>, 4>);
static_assert(is_mask_result<std::simd::resize_t<1, mask4>, int, 1>);
static_assert(is_mask_result<std::simd::resize_t<8, mask4>, int, 8>);
static_assert(is_mask_result<std::simd::resize_t<64, mask4>, int, 64>);

// Alignment is deliberately different: disabled basic_vec still has metadata
// for vectorizable U. These checks do not depend on the SIMD10 rounding policy.
static_assert(std::simd::alignment<disabled_value, int>::value >= alignof(int));
static_assert(std::simd::alignment<disabled_width, float>::value >= alignof(float));

constexpr bool enabled_results_remain_usable() {
    const std::simd::rebind_t<float, vector4> values(1.5f);
    const std::simd::resize_t<8, vector4> wide(7);
    const std::simd::rebind_t<double, mask4> selected([](auto lane) {
        return decltype(lane)::value % 2 == 0;
    });
    const std::simd::resize_t<8, mask4> all_selected(true);
    return values[0] == 1.5f && values[3] == 1.5f &&
        wide[0] == 7 && wide[7] == 7 &&
        selected[0] && !selected[1] && selected[2] && !selected[3] &&
        all_selected[0] && all_selected[7];
}

static_assert(enabled_results_remain_usable());

} // namespace

int main() {}
