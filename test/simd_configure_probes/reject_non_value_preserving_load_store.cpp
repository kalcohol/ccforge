#include <simd>

int main() {
    using V = std::simd::vec<int, 4>;

    V value(1);
    (void)std::simd::partial_load<V>(static_cast<const float*>(nullptr), std::simd::simd_size_type{4});
    std::simd::partial_store(value, static_cast<float*>(nullptr), std::simd::simd_size_type{4});
    return 0;
}
