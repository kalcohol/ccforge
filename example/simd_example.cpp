#include <simd>

#include <iostream>

int main() {
    std::simd::vec<float, 4> left([](auto index) {
        return static_cast<float>(decltype(index)::value + 1);
    });
    std::simd::vec<float, 4> right([](auto index) {
        return static_cast<float>(4 - decltype(index)::value);
    });

    const auto sum = left + right;
    const auto greater = left > right;

    std::cout << "sum = {"
              << sum[0] << ", " << sum[1] << ", "
              << sum[2] << ", " << sum[3] << "}\n";
    std::cout << "greater lanes = " << std::simd::reduce_count(greater) << '\n';

    return 0;
}
