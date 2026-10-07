#include <simd>

#include <bit>
#include <complex>
#include <cstddef>
#include <limits>
#include <type_traits>

#ifndef FORGE_BACKPORT_SIMD_HPP_INCLUDED
#error "This fixture checks the backport's ABI deduction policy."
#endif

namespace {

// [simd.expos.abi]/4 requires invalid deductions to name an unspecified type.
// [simd.overview]/1 and [simd.mask.overview]/1 specify the disabled members.
// [simd.traits]/1 also permits alignment queries on disabled vector types.
// https://eel.is/c++draft/simd.expos.abi#4

using size_type = std::simd::simd_size_type;

template<class T, size_type N>
concept has_deduced_abi = requires { typename std::simd::deduce_abi_t<T, N>; };

template<class V>
concept has_size = requires { V::size; };

template<class V>
concept has_iterator = requires { typename V::iterator; };

template<class V, class U>
concept has_alignment = requires { std::simd::alignment<V, U>::value; };

template<class V>
constexpr bool has_disabled_special_members =
    sizeof(V) > 0 &&
    !std::is_default_constructible_v<V> &&
    !std::is_destructible_v<V> &&
    !std::is_copy_constructible_v<V> &&
    !std::is_copy_assignable_v<V> &&
    !std::is_move_constructible_v<V> &&
    !std::is_move_assignable_v<V> &&
    !has_size<V> && !has_iterator<V>;

template<class T, size_type N>
constexpr bool disabled_aliases_have_metadata() {
    using abi = std::simd::deduce_abi_t<T, N>;
    using vector = std::simd::vec<T, N>;
    using mask = std::simd::mask<T, N>;

    return has_disabled_special_members<vector> &&
           has_disabled_special_members<mask> &&
           std::is_same_v<typename vector::value_type, T> &&
           std::is_same_v<typename vector::abi_type, abi> &&
           std::is_same_v<typename vector::mask_type, mask> &&
           std::is_same_v<typename mask::value_type, bool> &&
           std::is_same_v<typename mask::abi_type, abi> &&
           std::simd::simd_size<T, abi>::value == 0 &&
           std::simd::alignment_v<vector, int> >= alignof(int);
}

template<class T, size_type N>
constexpr bool enabled_aliases_are_unchanged() {
    using abi = std::simd::deduce_abi_t<T, N>;
    using vector = std::simd::vec<T, N>;
    using mask = std::simd::mask<T, N>;

    return std::is_default_constructible_v<vector> &&
           std::is_destructible_v<vector> &&
           std::is_trivially_copyable_v<vector> &&
           std::is_default_constructible_v<mask> &&
           std::is_trivially_copyable_v<mask> &&
           std::is_same_v<typename vector::value_type, T> &&
           std::is_same_v<typename vector::abi_type, abi> &&
           std::is_same_v<typename vector::mask_type, mask> &&
           vector::size == N && mask::size == N &&
           std::simd::simd_size<T, abi>::value == N &&
           std::simd::alignment_v<vector> == alignof(T) * N;
}

// These naming checks distinguish the original missing-type fallback without
// requiring construction or instantiating a fixed_size_abi with an invalid N.
static_assert(has_deduced_abi<int, 0>);
static_assert(has_deduced_abi<int, -1>);
static_assert(has_deduced_abi<int, 65>);
static_assert(has_deduced_abi<int, std::numeric_limits<size_type>::max()>);
static_assert(has_deduced_abi<long double, 2>);
static_assert(has_deduced_abi<bool, 4>);
static_assert(has_deduced_abi<void, 4>);

// The backport retains its existing maximum of 64 and unsupported-value policy.
static_assert(disabled_aliases_have_metadata<int, 0>());
static_assert(disabled_aliases_have_metadata<int, -1>());
static_assert(disabled_aliases_have_metadata<int, 65>());
static_assert(disabled_aliases_have_metadata<int, std::numeric_limits<size_type>::max()>());
static_assert(disabled_aliases_have_metadata<long double, 1>());
static_assert(disabled_aliases_have_metadata<long double, 2>());
static_assert(disabled_aliases_have_metadata<bool, 4>());
static_assert(disabled_aliases_have_metadata<int*, 4>());

using default_long_double = std::simd::vec<long double>;
using default_bool = std::simd::vec<bool>;
static_assert(has_disabled_special_members<default_long_double>);
static_assert(has_disabled_special_members<default_bool>);
static_assert(std::is_same_v<typename default_long_double::mask_type,
    std::simd::mask<long double>>);
static_assert(has_disabled_special_members<typename default_long_double::mask_type>);

using disabled_int65 = std::simd::vec<int, 65>;
using disabled_long_double2 = std::simd::vec<long double, 2>;
static_assert(has_alignment<disabled_int65, float>);
static_assert(has_alignment<disabled_long_double2, int>);
static_assert(!has_alignment<disabled_long_double2, long double>);
static_assert(!has_alignment<disabled_int65, bool>);
static_assert(std::simd::alignment_v<disabled_int65, float> >= alignof(float));
static_assert(std::has_single_bit(std::simd::alignment_v<disabled_int65, float>));
static_assert(std::is_base_of_v<std::integral_constant<std::size_t,
    std::simd::alignment_v<disabled_int65, float>>,
    std::simd::alignment<disabled_int65, float>>);

static_assert(enabled_aliases_are_unchanged<int, 1>());
static_assert(enabled_aliases_are_unchanged<int, 4>());
static_assert(enabled_aliases_are_unchanged<int, 8>());
static_assert(enabled_aliases_are_unchanged<int, 64>());
static_assert(enabled_aliases_are_unchanged<float, 4>());
static_assert(enabled_aliases_are_unchanged<double, 4>());
static_assert(enabled_aliases_are_unchanged<std::complex<double>, 4>());

using native_int = std::simd::basic_vec<int>;
static_assert(std::is_same_v<std::simd::deduce_abi_t<int, native_int::size>,
    std::simd::native_abi<int>>);
static_assert(std::is_same_v<std::simd::vec<int>, native_int>);
static_assert(std::is_default_constructible_v<native_int>);

constexpr bool enabled_construction_is_unchanged() {
    const std::simd::vec<int, 4> broadcast(7);
    const std::simd::vec<int, 4> generated([](auto i) {
        return static_cast<int>(decltype(i)::value + 1);
    });
    for (size_type i = 0; i < broadcast.size; ++i) {
        if (broadcast[i] != 7 || generated[i] != i + 1) {
            return false;
        }
    }
    return true;
}

static_assert(enabled_construction_is_unchanged());

} // namespace

int main() {}
