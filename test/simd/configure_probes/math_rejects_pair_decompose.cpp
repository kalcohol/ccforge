#include <simd>

int main() {
    using float4 = std::simd::vec<float, 4>;

    auto frexp_value = std::simd::frexp(float4{});
    auto modf_value = std::simd::modf(float4{});
    auto remquo_value = std::simd::remquo(float4{}, 2.0f);
    return static_cast<int>(frexp_value.first[0] + modf_value.first[0] + remquo_value.first[0]);
}
