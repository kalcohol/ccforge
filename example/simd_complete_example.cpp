#include <simd>

#include <iostream>
#include <tuple>

namespace {

template<class V>
auto lane(const V& value, std::simd::simd_size_type index) -> decltype(value[index]) {
    return value[index];
}

} // namespace

int main() {
    using int4 = std::simd::vec<int, 4>;
    using float4 = std::simd::rebind_t<float, int4>;
    using int8 = std::simd::resize_t<8, int4>;
    using mask4 = std::simd::mask<int, 4>;

    int4 values{1, 4, 2, 3};
    int4 generated([](auto index) {
        return static_cast<int>(decltype(index)::value + 1);
    });
    int4 offset(10);
    int8 widened(6);

    const auto sum = values + offset;
    const auto selected = sum > int4{10, 12, 12, 12};
    const auto reversed = std::simd::permute(sum, [](auto index, auto size) {
        return std::simd::simd_size_type(decltype(size)::value - 1 - decltype(index)::value);
    });
    const auto pieces = std::simd::chunk<int4>(std::simd::resize_t<8, int4>{1, 2, 3, 4, 5, 6, 7, 8});
    const auto stitched = std::simd::cat(pieces[0], pieces[1]);

    int raw[4] = {7, 8, 9, 10};
    float converted[4] = {};
    const auto loaded = std::simd::partial_load<int4>(raw, 3u);
    std::simd::partial_store(loaded, converted, 3u, std::simd::flag_convert);

    float4 rebound(values, std::simd::flag_convert);
    auto begin = generated.begin();
    auto end = generated.end();
    int generated_sum = 0;

    while (begin != end) {
        generated_sum += *begin;
        ++begin;
    }

    std::cout << "sum[0] = " << lane(sum, 0) << '\n';
    std::cout << "reduce(sum) = " << std::simd::reduce(sum) << '\n';
    std::cout << "reduce_min(sum) = " << std::simd::reduce_min(sum) << '\n';
    std::cout << "selected lanes = " << std::simd::reduce_count(selected) << '\n';
    std::cout << "reversed first/last = {" << lane(reversed, 0) << ", " << lane(reversed, 3) << "}\n";
    std::cout << "stitched first/last = {" << lane(stitched, 0) << ", " << lane(stitched, 7) << "}\n";
    std::cout << "generated sum = " << generated_sum << '\n';
    std::cout << "widened first/last = {" << lane(widened, 0) << ", " << lane(widened, 7) << "}\n";
    std::cout << "loaded tail = {" << lane(loaded, 0) << ", " << lane(loaded, 1) << ", "
              << lane(loaded, 2) << ", " << lane(loaded, 3) << "}\n";
    std::cout << "converted tail = {" << converted[0] << ", " << converted[1] << ", "
              << converted[2] << ", " << converted[3] << "}\n";
    std::cout << "rebound first = " << lane(rebound, 0) << '\n';
    std::cout << "mask first/last = {" << lane(mask4(selected), 0) << ", " << lane(mask4(selected), 3) << "}\n";

    return 0;
}
