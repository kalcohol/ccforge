// MIT License
//
// Copyright (c) 2026 Forge Project
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Demonstrates: SIMD memory, masks and permutations; backport-only spelling
// Key APIs: std::simd::vec, std::simd::reduce_count
// Requires: injected backport only; backport-only spelling; C++23
// See also: docs/backports/simd.md

#include <simd>

#include "example_support.hpp"
#include <iostream>
#include <array>
#include <tuple>

namespace {

template<class V>
auto lane(const V& value, decltype(std::simd::vec<int>::size()) index) -> decltype(value[index]) {
    return value[index];
}

} // namespace

int main() {
    using int4 = std::simd::vec<int, 4>;
    using float4 = std::simd::rebind_t<float, int4>;
    using int8 = std::simd::resize_t<8, int4>;
    using mask4 = std::simd::mask<int, 4>;

    const std::array<int, 4> values_raw{{1, 4, 2, 3}};
    int4 values = std::simd::partial_load<int4>(values_raw.data(), static_cast<decltype(std::simd::vec<int>::size())>(values_raw.size()));
    int4 generated([](auto index) {
        return static_cast<int>(decltype(index)::value + 1);
    });
    int4 offset(10);
    int8 widened(6);

    const auto sum = values + offset;
    const std::array<int, 4> threshold_raw{{10, 12, 12, 12}};
    const auto threshold = std::simd::partial_load<int4>(
        threshold_raw.data(),
        static_cast<decltype(std::simd::vec<int>::size())>(threshold_raw.size()));
    const auto selected = sum > threshold;
    const auto reversed = std::simd::permute(sum, [](auto index, auto size) {
        return decltype(std::simd::vec<int>::size())(size - 1 - index);
    });
    const int8 eight = std::simd::iota<int8> + int8(1);
    const auto pieces = std::simd::chunk<int4>(eight);
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

    forge_example::require(lane(sum, 0) == 11);
    forge_example::require(std::simd::reduce(sum) == 50);
    forge_example::require(std::simd::reduce_min(sum) == 11);
    forge_example::require(std::simd::reduce_count(selected) == 3);
    forge_example::require(lane(reversed, 0) == 13);
    forge_example::require(lane(reversed, 3) == 11);
    forge_example::require(lane(stitched, 0) == 1);
    forge_example::require(lane(stitched, 7) == 8);
    forge_example::require(generated_sum == 10);
    forge_example::require(lane(widened, 0) == 6);
    forge_example::require(lane(widened, 7) == 6);
    forge_example::require(lane(rebound, 0) == 1.0f);

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
