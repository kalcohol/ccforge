#include <simd>
#include <type_traits>

int main() {
    using float4 = std::simd::vec<float, 4>;
    using uint4 = std::simd::vec<unsigned, 4>;

    float4 values(0.5f);
    uint4 orders(1u);

    static_assert(std::is_same_v<decltype(std::simd::comp_ellint_1(values)), float4>);
    static_assert(std::is_same_v<decltype(std::simd::beta(values, 0.25f)), float4>);
    static_assert(std::is_same_v<decltype(std::simd::ellint_3(values, 0.25f, 0.5f)), float4>);
    static_assert(std::is_same_v<decltype(std::simd::hermite(orders, values)), float4>);
    static_assert(std::is_same_v<decltype(std::simd::assoc_laguerre(orders, orders, values)), float4>);

    auto comp = std::simd::comp_ellint_1(values);
    auto beta_value = std::simd::beta(values, 0.25f);
    auto hermite_value = std::simd::hermite(orders, values);
    auto assoc_value = std::simd::assoc_laguerre(orders, orders, values);

    return static_cast<int>(comp[0] + beta_value[0] + hermite_value[0] + assoc_value[0]);
}
