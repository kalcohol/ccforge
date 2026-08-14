#include <simd>

struct stateful_index_map {
    std::simd::simd_size_type offset;

    template<class Index>
    constexpr std::simd::simd_size_type operator()(Index index) const noexcept {
        return Index::value + offset;
    }
};

int main() {
    const std::simd::vec<int, 4> values(1);
    const stateful_index_map map{0};
    const auto permuted = std::simd::permute(values, map);
    return permuted[0];
}
