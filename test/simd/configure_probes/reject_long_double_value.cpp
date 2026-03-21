#include <simd>

int main() {
    std::simd::vec<long double, 2> value(1.0L);
    return static_cast<int>(value[0]);
}
