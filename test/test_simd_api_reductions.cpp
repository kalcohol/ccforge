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

} // namespace

int main() {
    const simd_test::int4 values([](auto lane) {
        return static_cast<int>(decltype(lane)::value + 1);
    });
    const simd_test::mask4 selected(0b0101u);

    const int reduced = std::simd::reduce(values, simd_plus{});
    const int masked_and = std::simd::reduce(values, selected, std::bit_and<>{});
    const int masked_or = std::simd::reduce(values, selected, std::bit_or<>{});
    const int masked_xor = std::simd::reduce(values, selected, std::bit_xor<>{});

    return reduced == 10 && masked_and == (1 & 3) && masked_or == (1 | 3) && masked_xor == (1 ^ 3) ? 0 : 1;
}
