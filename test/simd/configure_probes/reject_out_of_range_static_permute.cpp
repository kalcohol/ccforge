#include <simd>

#include <type_traits>

struct out_of_range_index_map {
    constexpr std::simd::__simd_size_type operator()(std::simd::__simd_size_type) const noexcept {
#if defined(FORGE_SIMD_POSITIVE_CONTROL)
        return 3;
#else
        return 7;
#endif
    }
};

static_assert(std::is_same_v<std::invoke_result_t<const out_of_range_index_map&,
    std::simd::__simd_size_type>, std::simd::__simd_size_type>);

int main() {
    const std::simd::vec<int, 4> values(1);
    const auto permuted = std::simd::permute(values, out_of_range_index_map{});
    return permuted[0];
}
