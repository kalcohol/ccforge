#include <simd>

int main() {
    std::simd::vec<int, 4> value(1);
    (void)std::simd::simd_cast<std::simd::vec<float, 4>>(value);
}
