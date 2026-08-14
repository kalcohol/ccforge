#include "simd_test_common.hpp"

#include <functional>

namespace {

using namespace simd_test;

struct simd_plus {
    template<class T, class Abi>
    constexpr std::simd::basic_vec<T, Abi> operator()(const std::simd::basic_vec<T, Abi>& left,
                                                      const std::simd::basic_vec<T, Abi>& right) const noexcept {
        return left + right;
    }
};

static_assert(std::is_same<decltype(std::simd::reduce(std::declval<const int4&>())), int>::value,
    "reduce(vec) should return the scalar lane type");
static_assert(std::is_same<decltype(std::simd::reduce(std::declval<const int4&>(), std::declval<const mask4&>())), int>::value,
    "reduce(vec, mask) should return the scalar lane type");
static_assert(std::is_same_v<decltype(std::simd::reduce(1)), int>,
    "reduce(scalar) should preserve the scalar type");
static_assert(std::is_same_v<decltype(std::simd::reduce_min(1, false)), int>,
    "reduce_min(scalar, bool) should preserve the scalar type");
static_assert(std::is_same_v<decltype(std::simd::reduce_max(1, false)), int>,
    "reduce_max(scalar, bool) should preserve the scalar type");

constexpr bool reductions_produce_expected_values() {
    const int4 values([](auto lane) {
        return static_cast<int>(decltype(lane)::value + 1);
    });
    const mask4 selected(0b0101u);

    const int reduced = std::simd::reduce(values, simd_plus{});
    const int masked_and = std::simd::reduce(values, selected, std::bit_and<>{});
    const int masked_or = std::simd::reduce(values, selected, std::bit_or<>{});
    const int masked_xor = std::simd::reduce(values, selected, std::bit_xor<>{});
    const int scalar_sum = std::simd::reduce(7);
    const int scalar_identity = std::simd::reduce(7, false);

    return reduced == 10 &&
           masked_and == (1 & 3) &&
           masked_or == (1 | 3) &&
           masked_xor == (1 ^ 3) &&
           scalar_sum == 7 &&
           scalar_identity == 0;
}

static_assert(reductions_produce_expected_values(),
    "std::simd reductions should preserve their constexpr values");

} // namespace

int main() {}
