#include <simd>

int main() {
    std::simd::basic_vec<__int128> value;
    return static_cast<int>(value[0]);
}
