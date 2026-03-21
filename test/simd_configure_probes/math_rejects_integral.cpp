#include <simd>

int main() {
    std::simd::vec<int, 4> values(1);
    auto bad = std::simd::sin(values);
    return bad[0];
}
