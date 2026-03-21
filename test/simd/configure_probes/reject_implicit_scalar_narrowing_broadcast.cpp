#include <simd>

int main() {
    std::simd::vec<float, 4> values = 1;
    return static_cast<int>(values[0]);
}
