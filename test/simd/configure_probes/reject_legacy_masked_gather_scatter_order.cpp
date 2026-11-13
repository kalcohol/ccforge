#include <simd>

#include <array>

#if defined(FORGE_SIMD_PROBE_OPERATION) && (FORGE_SIMD_PROBE_OPERATION < 0 || FORGE_SIMD_PROBE_OPERATION > 3)
#error Invalid gather/scatter operation
#endif

int main() {
    std::array<int, 8> data{{10, 11, 12, 13, 14, 15, 16, 17}};
    const std::simd::vec<int, 4> indices([](auto lane) {
        constexpr int values[] = {6, 0, 3, 7};
        return values[decltype(lane)::value];
    });
    const std::simd::mask<int, 4> selected(0b0101u);
    std::simd::vec<int, 4> values{};

#if !defined(FORGE_SIMD_PROBE_OPERATION) || FORGE_SIMD_PROBE_OPERATION == 0
#if defined(FORGE_SIMD_POSITIVE_CONTROL)
    auto gathered = std::simd::partial_gather_from<std::simd::vec<int, 4>>(
        data.data(), std::simd::__simd_size_type{8}, selected, indices);
#else
    auto gathered = std::simd::partial_gather_from<std::simd::vec<int, 4>>(
        data.data(),
        std::simd::__simd_size_type{8},
        indices,
        selected);
#endif
#endif
#if !defined(FORGE_SIMD_PROBE_OPERATION) || FORGE_SIMD_PROBE_OPERATION == 1
#if defined(FORGE_SIMD_POSITIVE_CONTROL)
    auto unchecked = std::simd::unchecked_gather_from<std::simd::vec<int, 4>>(
        data.data(), selected, indices);
#else
    auto unchecked = std::simd::unchecked_gather_from<std::simd::vec<int, 4>>(
        data.data(),
        indices,
        selected);
#endif
#endif
#if !defined(FORGE_SIMD_PROBE_OPERATION) || FORGE_SIMD_PROBE_OPERATION == 2
#if defined(FORGE_SIMD_POSITIVE_CONTROL)
    std::simd::partial_scatter_to(values, data.data(), std::simd::__simd_size_type{8}, selected, indices);
#else
    std::simd::partial_scatter_to(values, data.data(), std::simd::__simd_size_type{8}, indices, selected);
#endif
#endif
#if !defined(FORGE_SIMD_PROBE_OPERATION) || FORGE_SIMD_PROBE_OPERATION == 3
#if defined(FORGE_SIMD_POSITIVE_CONTROL)
    std::simd::unchecked_scatter_to(values, data.data(), selected, indices);
#else
    std::simd::unchecked_scatter_to(values, data.data(), indices, selected);
#endif
#endif
#if defined(FORGE_SIMD_PROBE_OPERATION)
    return 0;
#else
    return gathered[0] + unchecked[0];
#endif
}
