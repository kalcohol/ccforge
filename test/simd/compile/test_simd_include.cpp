#include <simd>

int main() {
    std::simd::vec<int, 4> value(1);
    const auto mask = value == value;
    return std::simd::all_of(mask) ? 0 : 1;
}
