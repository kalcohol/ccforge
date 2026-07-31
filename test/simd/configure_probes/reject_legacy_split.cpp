#include <simd>

int main() {
    std::simd::vec<int, 4> value(1);
    (void)std::simd::split<std::simd::vec<int, 2>>(value);
}
