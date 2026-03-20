#include <simd>
#include <type_traits>

int main() {
    static_assert(!std::is_constructible<std::simd::vec<float, 4>, std::simd::vec<int, 4>>::value,
        "Implicit vector conversion should be disabled when the compile probe fails");
    static_assert(std::is_constructible<std::simd::vec<float, 4>, std::simd::vec<int, 4>, std::simd::flags<std::simd::convert_flag>>::value,
        "Flag-convert constructor should remain available");
    return 0;
}
