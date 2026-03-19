#include <simd>

#include <iostream>

int main() {
    std::simd::vec<float, 4> left{1.0f, 2.0f, 3.0f, 4.0f};
    std::simd::vec<float, 4> right{4.0f, 3.0f, 2.0f, 1.0f};

    const auto sum = left + right;
    const auto greater = left > right;

    std::cout << "sum = {"
              << sum[0] << ", " << sum[1] << ", "
              << sum[2] << ", " << sum[3] << "}\n";
    std::cout << "greater lanes = " << std::simd::reduce_count(greater) << '\n';

    return 0;
}
