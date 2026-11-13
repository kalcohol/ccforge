#include <simd>

#if defined(FORGE_SIMD_PROBE_OPERATION) && (FORGE_SIMD_PROBE_OPERATION < 0 || FORGE_SIMD_PROBE_OPERATION > 1)
#error Invalid load/store operation
#endif

int main() {
    using V = std::simd::vec<int, 4>;

    V value(1);
#if defined(FORGE_SIMD_POSITIVE_CONTROL)
    float data[4]{};
#endif
#if !defined(FORGE_SIMD_PROBE_OPERATION) || FORGE_SIMD_PROBE_OPERATION == 0
#if defined(FORGE_SIMD_POSITIVE_CONTROL)
    (void)std::simd::partial_load<V>(data, std::simd::__simd_size_type{4}, std::simd::flag_convert);
#else
    (void)std::simd::partial_load<V>(static_cast<const float*>(nullptr), std::simd::__simd_size_type{4});
#endif
#endif
#if !defined(FORGE_SIMD_PROBE_OPERATION) || FORGE_SIMD_PROBE_OPERATION == 1
#if defined(FORGE_SIMD_POSITIVE_CONTROL)
    std::simd::partial_store(value, data, std::simd::__simd_size_type{4}, std::simd::flag_convert);
#else
    std::simd::partial_store(value, static_cast<float*>(nullptr), std::simd::__simd_size_type{4});
#endif
#endif
    return 0;
}
