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

// Demonstrates: linalg simd
// Key APIs: std::mdspan, std::linalg::dot
// Requires: LINALG: injected backport or complete native; mdspan ; C++23
// See also: docs/backports/linalg.md

#include <linalg>
#include <mdspan>
#include "example_support.hpp"
#include <cmath>

int main() {
    constexpr int N = 256;
    double x[N], y[N];
    for (int i = 0; i < N; ++i) { x[i] = static_cast<double>(i+1); y[i] = 1.0/(i+1); }

    std::mdspan xv(x, std::extents<int, N>{});
    std::mdspan yv(y, std::extents<int, N>{});

    // dot product (SIMD fast path when simd backport available)
    double d = std::linalg::dot(xv, yv, 0.0);
    forge_example::require(std::abs(d - N) < 1e-10);

    // vector_two_norm (SIMD fast path)
    float fdata[4] = {3.0f, 4.0f, 0.0f, 0.0f};
    std::mdspan fv(fdata, std::extents<int, 4>{});
    float nrm = std::linalg::vector_two_norm(fv);
    forge_example::require(std::abs(nrm - 5.0f) < 1e-5f);

    return 0;
}
