#include <simd>

struct out_of_range_index_map {
    template<class Index>
    constexpr std::simd::simd_size_type operator()(Index) const noexcept {
        return 7;
    }
};

int main() {
    const std::simd::vec<int, 4> values(1);
    const auto permuted = std::simd::permute(values, out_of_range_index_map{});
    return permuted[0];
}
