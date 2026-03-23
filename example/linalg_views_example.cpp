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

#include <version>
#if defined(__cpp_lib_mdspan)
#include <linalg>
#include <mdspan>
#endif
#include <cassert>
int main() {
#if defined(__cpp_lib_mdspan)
    int a[] = {1, 2, 3, 4, 5, 6, 7, 8, 9}, x[] = {1, 0, 1}, y[3]{}, z[3]{};
    std::mdspan A(a, std::extents<int, 3, 3>{}); std::mdspan xv(x, std::extents<int, 3>{}); std::mdspan yv(y, std::extents<int, 3>{}); std::mdspan zv(z, std::extents<int, 3>{}); auto As = std::linalg::scaled(2.0, A); auto At = std::linalg::transposed(A);
    std::linalg::matrix_vector_product(As, xv, yv); std::linalg::matrix_vector_product(At, xv, zv); assert(y[0] == 8 && y[1] == 20 && y[2] == 32 && z[0] == 8 && z[1] == 10 && z[2] == 12);
#endif
    return 0; }
