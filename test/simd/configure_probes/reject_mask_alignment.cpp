#include <simd>

int main() {
    constexpr auto alignment = std::simd::alignment_v<std::simd::mask<int, 4>, bool>;
    return static_cast<int>(alignment);
}
