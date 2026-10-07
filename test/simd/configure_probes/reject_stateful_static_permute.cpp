#include <simd>

#include <type_traits>

struct stateful_index_map {
    std::simd::simd_size_type offset;

    constexpr std::simd::simd_size_type operator()(std::simd::simd_size_type index) const noexcept {
        return index + offset;
    }
};

static_assert(std::is_same_v<std::invoke_result_t<const stateful_index_map&,
    std::simd::simd_size_type>, std::simd::simd_size_type>);

int main() {
    const std::simd::vec<int, 4> values(1);
    const stateful_index_map map{0};
    const auto permuted = std::simd::permute(values, map);
    return permuted[0];
}
