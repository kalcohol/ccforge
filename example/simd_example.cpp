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

// Demonstrates: simd
// Key APIs: std::simd::vec, std::simd::reduce_count
// Requires: SIMD: injected backport or complete native ; C++23
// See also: docs/backports/simd.md

#include <simd>

#include "example_support.hpp"
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

    forge_example::require(sum[0] == 5.0f);
    forge_example::require(sum[1] == 5.0f);
    forge_example::require(sum[2] == 5.0f);
    forge_example::require(sum[3] == 5.0f);
    forge_example::require(std::simd::reduce_count(greater) == 2);

    std::cout << "sum = {"
              << sum[0] << ", " << sum[1] << ", "
              << sum[2] << ", " << sum[3] << "}\n";
    std::cout << "greater lanes = " << std::simd::reduce_count(greater) << '\n';

    return 0;
}
