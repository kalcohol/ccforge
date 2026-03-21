#include <simd>
#include <type_traits>

int main() {
    using float4 = std::simd::vec<float, 4>;
    using int4 = std::simd::rebind_t<int, float4>;
    using long4 = std::simd::rebind_t<long int, float4>;

    float4 values(1.0f);
    int4 exponents(0);
    long4 long_exponents(0);

    static_assert(std::is_same_v<decltype(std::simd::atan2(values, 2.0f)), float4>);
    static_assert(std::is_same_v<decltype(std::simd::exp2(values)), float4>);
    static_assert(std::is_same_v<decltype(std::simd::fpclassify(values)), int4>);
    static_assert(std::is_same_v<decltype(std::simd::frexp(values, &exponents)), float4>);
    static_assert(std::is_same_v<decltype(std::simd::modf(values, &values)), float4>);
    static_assert(std::is_same_v<decltype(std::simd::remquo(values, 2.0f, &exponents)), float4>);
    static_assert(std::is_same_v<decltype(std::simd::scalbln(values, long_exponents)), float4>);

    auto log10_value = std::simd::log10(values);
    auto atan2_value = std::simd::atan2(values, 2.0f);
    auto exp2_value = std::simd::exp2(values);
    auto erf_value = std::simd::erf(values);
    auto lerp_value = std::simd::lerp(values, 2.0f, 0.5f);
    auto class_value = std::simd::fpclassify(values);
    auto finite_mask = std::simd::isfinite(values);
    auto frexp_value = std::simd::frexp(values, &exponents);
    auto remquo_value = std::simd::remquo(values, 2.0f, &exponents);
    auto modf_value = std::simd::modf(values, &values);

    return static_cast<int>(
        log10_value[0] +
        atan2_value[0] +
        exp2_value[0] +
        erf_value[0] +
        lerp_value[0] +
        static_cast<float>(class_value[0]) +
        (finite_mask[0] ? 1.0f : 0.0f) +
        frexp_value[0] +
        remquo_value[0] +
        modf_value[0]);
}
