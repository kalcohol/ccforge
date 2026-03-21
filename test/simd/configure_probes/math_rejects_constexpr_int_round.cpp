#include <simd>

constexpr long int probe_lrint() {
    return std::simd::lrint(std::simd::vec<float, 4>(1.25f))[0];
}

constexpr long long int probe_llrint() {
    return std::simd::llrint(std::simd::vec<float, 4>(1.25f))[0];
}

constexpr long int kLrint = probe_lrint();
constexpr long long int kLlrint = probe_llrint();

int main() {
    return static_cast<int>(kLrint + static_cast<long int>(kLlrint));
}
