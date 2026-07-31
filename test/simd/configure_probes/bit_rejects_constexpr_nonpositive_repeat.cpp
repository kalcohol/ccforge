#include <simd>

constexpr auto repeated =
    std::simd::bit_repeat(std::simd::vec<unsigned, 4>(1u), -1);

int main() {
    return static_cast<int>(repeated[0]);
}
