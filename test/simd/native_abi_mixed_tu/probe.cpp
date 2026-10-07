#include "probe.hpp"

#include <type_traits>

namespace native_abi_fixture {

namespace {

using native_tag = std::simd::native_abi<int>;
using native_vec = std::simd::basic_vec<int, native_tag>;
using native_mask = native_vec::mask_type;

static_assert(native_vec::size() == FORGE_ABI_EXPECTED_LANES);
static_assert(native_mask::size() == FORGE_ABI_EXPECTED_LANES);
static_assert(std::simd::abi_lane_count<native_tag>::value == FORGE_ABI_EXPECTED_LANES);
static_assert(std::is_same_v<native_vec, std::simd::vec<int>>);
static_assert(std::is_same_v<native_mask, std::simd::mask<int>>);
static_assert(std::is_same_v<native_tag, std::simd::deduce_abi_t<int, FORGE_ABI_EXPECTED_LANES>>);
static_assert(std::is_same_v<typename native_vec::abi_type, native_tag>);
static_assert(sizeof(native_vec) == sizeof(int) * FORGE_ABI_EXPECTED_LANES);

template<class V>
bool check_float_values() {
    const V squares([](auto i) {
        const float root = static_cast<float>(i) + 1.0f;
        return root * root;
    });
    const V negatives([](auto i) { return -(static_cast<float>(i) + 1.0f); });
    const auto roots = std::simd::sqrt(squares);
    const auto magnitudes = std::simd::abs(negatives);
    const auto finite = std::simd::isfinite(roots);
    for (std::simd::simd_size_type i = 0; i < V::size(); ++i) {
        const float expected = static_cast<float>(i) + 1.0f;
        if (roots[i] != expected || magnitudes[i] != expected || !finite[i]) {
            return false;
        }
    }
    return true;
}

} // namespace

snapshot FORGE_ABI_PROBE() noexcept {
    const native_vec value = native_vec(1) + native_vec(2);
    const native_mask selected = value == native_vec(3);
    bool values_ok = true;
    for (std::simd::simd_size_type i = 0; i < native_vec::size(); ++i) {
        values_ok = values_ok && value[i] == 3 && selected[i];
    }
    return {&typeid(native_tag), &typeid(native_vec), &typeid(native_mask),
        &typeid(narrow_float_vec), &typeid(wide_float_vec),
        static_cast<std::size_t>(native_vec::size()), sizeof(native_vec),
        sizeof(native_mask), values_ok,
        check_float_values<narrow_float_vec>(), check_float_values<wide_float_vec>()};
}

narrow_vec FORGE_ABI_NARROW(narrow_vec value) noexcept {
    return value + narrow_vec(1);
}

wide_vec FORGE_ABI_WIDE(wide_vec value) noexcept {
    return value + wide_vec(1);
}

narrow_float_vec FORGE_ABI_FLOAT_NARROW(narrow_float_vec value) noexcept {
    return std::simd::abs(value);
}

wide_float_vec FORGE_ABI_FLOAT_WIDE(wide_float_vec value) noexcept {
    return std::simd::abs(value);
}

} // namespace native_abi_fixture
