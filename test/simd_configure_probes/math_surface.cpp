#include <simd>

int main() {
    using float4 = std::simd::vec<float, 4>;

    float4 values(1.0f);
    auto log10_value = std::simd::log10(values);
    auto pow_value = std::simd::pow(values, 2.0f);
    auto hypot_value = std::simd::hypot(values, 2.0f);
    auto finite_mask = std::simd::isfinite(values);
    auto frexp_value = std::simd::frexp(values);
    auto remquo_value = std::simd::remquo(values, 2.0f);
    auto modf_value = std::simd::modf(values);

    return static_cast<int>(
        log10_value[0] +
        pow_value[0] +
        hypot_value[0] +
        (finite_mask[0] ? 1.0f : 0.0f) +
        frexp_value.first[0] +
        static_cast<float>(frexp_value.second[0]) +
        remquo_value.first[0] +
        static_cast<float>(remquo_value.second[0]) +
        modf_value.first[0] +
        modf_value.second[0]);
}
