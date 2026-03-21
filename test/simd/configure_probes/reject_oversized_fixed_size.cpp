#include <simd>

int main() {
    std::simd::vec<int, 65> value(0);
    return value[0];
}
