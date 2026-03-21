#include <simd>

int main() {
    std::simd::vec<int, 4> ints(1);
    std::simd::vec<float, 4> converted(ints, std::simd::flag_convert);
    (void)converted;
    return 0;
}
