#include "probe.hpp"

#include <iostream>

int main() {
    using namespace native_abi_fixture;

    if constexpr (FORGE_ABI_FIRST_LANES == 8 || FORGE_ABI_SECOND_LANES == 8) {
        if (!__builtin_cpu_supports("avx2")) {
            return 77;
        }
    }

    const snapshot first = probe_first();
    const snapshot second = probe_second();
    constexpr bool same_width = FORGE_ABI_FIRST_LANES == FORGE_ABI_SECOND_LANES;
    if ((*first.abi_type == *second.abi_type) != same_width ||
        (*first.vec_type == *second.vec_type) != same_width ||
        (*first.mask_type == *second.mask_type) != same_width) {
        std::cerr << "Native ABI tag, vector, or mask identity does not match its width\n";
        return 1;
    }
    if (*first.narrow_float_type != *second.narrow_float_type ||
        *first.wide_float_type != *second.wide_float_type ||
        !first.narrow_float_values_ok || !second.narrow_float_values_ok ||
        !first.wide_float_values_ok || !second.wide_float_values_ok) {
        std::cerr << "Pinned floating ABI identity or math values changed between TUs\n";
        return 1;
    }
    if (first.lanes != FORGE_ABI_FIRST_LANES || second.lanes != FORGE_ABI_SECOND_LANES ||
        first.vec_bytes != sizeof(int) * first.lanes ||
        second.vec_bytes != sizeof(int) * second.lanes ||
        first.mask_bytes != sizeof(bool) * first.lanes ||
        second.mask_bytes != sizeof(bool) * second.lanes ||
        !first.values_ok || !second.values_ok) {
        std::cerr << "Native ABI width, storage, or lane values changed\n";
        return 1;
    }

    const narrow_vec narrow = roundtrip_narrow_second(roundtrip_narrow_first(narrow_vec(5)));
    const wide_vec wide = roundtrip_wide_second(roundtrip_wide_first(wide_vec(5)));
    for (std::simd::simd_size_type i = 0; i < narrow_vec::size(); ++i) {
        if (narrow[i] != 7) {
            std::cerr << "Narrow native ABI frame did not survive the TU boundary\n";
            return 1;
        }
    }
    for (std::simd::simd_size_type i = 0; i < wide_vec::size(); ++i) {
        if (wide[i] != 7) {
            std::cerr << "Wide native ABI frame did not survive the TU boundary\n";
            return 1;
        }
    }
    const narrow_float_vec negative_narrow([](auto i) { return -(static_cast<float>(i) + 1.0f); });
    const wide_float_vec negative_wide([](auto i) { return -(static_cast<float>(i) + 1.0f); });
    const auto narrow_abs = roundtrip_float_narrow_second(roundtrip_float_narrow_first(negative_narrow));
    const auto wide_abs = roundtrip_float_wide_second(roundtrip_float_wide_first(negative_wide));
    for (std::simd::simd_size_type i = 0; i < narrow_float_vec::size(); ++i) {
        if (narrow_abs[i] != static_cast<float>(i) + 1.0f) {
            std::cerr << "Narrow floating ABI frame did not survive the TU boundary\n";
            return 1;
        }
    }
    for (std::simd::simd_size_type i = 0; i < wide_float_vec::size(); ++i) {
        if (wide_abs[i] != static_cast<float>(i) + 1.0f) {
            std::cerr << "Wide floating ABI frame did not survive the TU boundary\n";
            return 1;
        }
    }
    return 0;
}
