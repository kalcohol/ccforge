#include <simd>

#if defined(FORGE_SIMD_PROBE_OPERATION) && (FORGE_SIMD_PROBE_OPERATION < 0 || FORGE_SIMD_PROBE_OPERATION > 1)
#error Invalid integer rounding operation
#endif

#if !defined(FORGE_SIMD_PROBE_OPERATION) || FORGE_SIMD_PROBE_OPERATION == 0
#if defined(FORGE_SIMD_POSITIVE_CONTROL)
long int probe_lrint() {
#else
constexpr long int probe_lrint() {
#endif
    return std::simd::lrint(std::simd::vec<float, 4>(1.25f))[0];
}
#if defined(FORGE_SIMD_POSITIVE_CONTROL)
const long int kLrint = probe_lrint();
#else
constexpr long int kLrint = probe_lrint();
#endif
#endif

#if !defined(FORGE_SIMD_PROBE_OPERATION) || FORGE_SIMD_PROBE_OPERATION == 1
#if defined(FORGE_SIMD_POSITIVE_CONTROL)
long long int probe_llrint() {
#else
constexpr long long int probe_llrint() {
#endif
    return std::simd::llrint(std::simd::vec<float, 4>(1.25f))[0];
}

#if defined(FORGE_SIMD_POSITIVE_CONTROL)
const long long int kLlrint = probe_llrint();
#else
constexpr long long int kLlrint = probe_llrint();
#endif
#endif

int main() {
#if !defined(FORGE_SIMD_PROBE_OPERATION)
    return static_cast<int>(kLrint + static_cast<long int>(kLlrint));
#elif FORGE_SIMD_PROBE_OPERATION == 0
    return static_cast<int>(kLrint);
#else
    return static_cast<int>(kLlrint);
#endif
}
