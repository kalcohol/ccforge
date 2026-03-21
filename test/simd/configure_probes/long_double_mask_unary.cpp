#include <simd>

int main() {
    std::simd::vec<long double, 2> values(1.0L);
    auto mask = values == values;
    auto positive = +mask;
    auto negative = -mask;
    auto inverted = ~mask;
    return static_cast<int>(positive[0] + negative[0] + inverted[0]);
}
