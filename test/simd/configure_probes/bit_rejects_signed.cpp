#include <simd>

int main() {
    using int4 = std::simd::vec<int, 4>;
    auto values = std::simd::bit_floor(int4{});
    return values[0];
}
