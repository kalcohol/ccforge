# std::simd (P1928). Probe a core declaration rather than header presence:
# some libraries install an empty <simd> outside C++26 mode.
check_cxx_source_compiles("
    #include <simd>
    #include <array>
    #include <bit>
    #include <cmath>
    #include <complex>
    #include <span>
    #if !defined(__cpp_lib_simd) || __cpp_lib_simd < 202606L
    #error incomplete simd core surface
    #endif
    #if !defined(__cpp_lib_simd_bitops) || __cpp_lib_simd_bitops < 202607L
    #error incomplete simd bit surface
    #endif
    #if !defined(__cpp_lib_simd_complex) || __cpp_lib_simd_complex < 202502L
    #error incomplete simd complex surface
    #endif
    #if !defined(__cpp_lib_simd_permutations) || __cpp_lib_simd_permutations < 202506L
    #error incomplete simd permutation surface
    #endif
    int main() {
        using float4 = std::simd::vec<float, 4>;
        using uint4 = std::simd::vec<unsigned, 4>;
        using complex4 = std::simd::vec<std::complex<float>, 4>;
        float4 v(1.0f);
        uint4 bits(1u);
        auto sum = std::simd::reduce(v);
        auto mask = v == v;
        auto math = std::simd::sin(v);
        auto top_level_math = std::sin(v);
        auto counted = std::simd::popcount(bits);
        auto top_level_counted = std::popcount(bits);
        complex4 complex_values(float4(1.0f), float4(2.0f));
        auto real_values = std::simd::real(complex_values);
        auto sequence = std::simd::iota<float4>;
        std::array<float, 8> input{};
        std::array<float, 8> output{};
        std::span<const float, 8> input_view(input);
        std::simd::vec<int, 4> indices(0);
        auto loaded = std::simd::partial_load<float4>(input.data(), 4);
        std::simd::partial_store(loaded, output.data(), 4);
        auto unchecked = std::simd::unchecked_load<float4>(input.data());
        std::simd::unchecked_store(unchecked, output.data());
        auto gathered = std::simd::partial_gather_from(input_view, indices);
        std::simd::partial_scatter_to(gathered, output.data(), 8, indices);
        auto permuted = std::simd::permute(v, indices);
        return static_cast<int>(
            sum + std::simd::reduce_count(mask) + math[0] +
            top_level_math[0] + static_cast<float>(counted[0]) +
            static_cast<float>(top_level_counted[0]) + real_values[0] +
            sequence[0] + gathered[0] + permuted[0] + output[0]);
    }
" FORGE_SIMD_FULL)
check_cxx_source_compiles("
    #include <simd>
    #ifndef __cpp_lib_simd
    #error no simd feature macro
    #endif
    int main() { return 0; }
" FORGE_SIMD_PARTIAL_MACRO)
check_cxx_source_compiles("
    #include <simd>
    using probe = std::simd::basic_vec<float>;
    int main() {
        probe* p = nullptr;
        (void)p;
        return 0;
    }
" FORGE_SIMD_PARTIAL_VEC)
check_cxx_source_compiles("
    #include <simd>
    #include <cstddef>
    template<template<std::size_t, class> class>
    struct accepts_mask_template {};
    using probe = accepts_mask_template<std::simd::basic_mask>;
    int main() {
        probe* p = nullptr;
        (void)p;
        return 0;
    }
" FORGE_SIMD_PARTIAL_MASK)
set(FORGE_SIMD_PARTIAL FALSE)
if(FORGE_SIMD_PARTIAL_MACRO OR FORGE_SIMD_PARTIAL_VEC OR FORGE_SIMD_PARTIAL_MASK)
    set(FORGE_SIMD_PARTIAL TRUE)
endif()
_forge_decide("std::simd" SIMD FORGE_SIMD_FULL FORGE_SIMD_PARTIAL)
