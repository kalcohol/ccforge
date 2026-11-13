#include <simd>

#include <deque>
#include <array>

int main() {
#if defined(FORGE_SIMD_POSITIVE_CONTROL)
    std::array<int, 4> input{1, 2, 3, 4};
#else
    std::deque<int> input{1, 2, 3, 4};
#endif
    auto values = std::simd::partial_load<std::simd::vec<int, 4>>(input.begin(), std::simd::__simd_size_type{4});
    return values[0];
}
