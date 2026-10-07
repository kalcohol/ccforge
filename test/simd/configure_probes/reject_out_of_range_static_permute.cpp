#include <simd>

#include <type_traits>

struct out_of_range_index_map {
    constexpr std::simd::simd_size_type operator()(std::simd::simd_size_type) const noexcept {
        return 7;
    }
};

static_assert(std::is_same_v<std::invoke_result_t<const out_of_range_index_map&,
    std::simd::simd_size_type>, std::simd::simd_size_type>);

int main() {
    const std::simd::vec<int, 4> values(1);
    const auto permuted = std::simd::permute(values, out_of_range_index_map{});
    return permuted[0];
}
