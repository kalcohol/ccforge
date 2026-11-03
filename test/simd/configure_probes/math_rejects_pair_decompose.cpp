#include <simd>

#if defined(FORGE_SIMD_PROBE_OPERATION) && (FORGE_SIMD_PROBE_OPERATION < 0 || FORGE_SIMD_PROBE_OPERATION > 2)
#error Invalid decomposition operation
#endif

int main() {
    using float4 = std::simd::vec<float, 4>;

#if !defined(FORGE_SIMD_PROBE_OPERATION) || FORGE_SIMD_PROBE_OPERATION == 0
#if defined(FORGE_SIMD_POSITIVE_CONTROL)
    std::simd::vec<int, 4> exponent;
    auto frexp_value = std::simd::frexp(float4{}, &exponent);
#else
    auto frexp_value = std::simd::frexp(float4{});
#endif
#endif
#if !defined(FORGE_SIMD_PROBE_OPERATION) || FORGE_SIMD_PROBE_OPERATION == 1
#if defined(FORGE_SIMD_POSITIVE_CONTROL)
    float4 integral;
    auto modf_value = std::simd::modf(float4{}, &integral);
#else
    auto modf_value = std::simd::modf(float4{});
#endif
#endif
#if !defined(FORGE_SIMD_PROBE_OPERATION) || FORGE_SIMD_PROBE_OPERATION == 2
#if defined(FORGE_SIMD_POSITIVE_CONTROL)
    std::simd::vec<int, 4> quotient;
    auto remquo_value = std::simd::remquo(float4{}, 2.0f, &quotient);
#else
    auto remquo_value = std::simd::remquo(float4{}, 2.0f);
#endif
#endif
#if defined(FORGE_SIMD_PROBE_OPERATION) || defined(FORGE_SIMD_POSITIVE_CONTROL)
    return 0;
#else
    return static_cast<int>(frexp_value.first[0] + modf_value.first[0] + remquo_value.first[0]);
#endif
}
