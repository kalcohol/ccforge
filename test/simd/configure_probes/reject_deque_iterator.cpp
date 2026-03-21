#include <simd>

#include <deque>

int main() {
    std::deque<int> input{1, 2, 3, 4};
    auto values = std::simd::partial_load<std::simd::vec<int, 4>>(input.begin(), std::simd::simd_size_type{4});
    return values[0];
}
