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

// Demonstrates: Row, column and strided slices with boundary cells
// Key APIs: std::submdspan, std::range_slice
// Requires: SUBMDSPAN: injected backport or complete native; mdspan ; C++23
// See also: docs/backports/mdspan.md

#include <mdspan>
#include <array>
#include <iostream>
#include "example_support.hpp"

int main() {
    std::array<int, 24> data{};
    for (int i = 0; i < 24; ++i) data[i] = i + 1;
    std::mdspan matrix(data.data(), std::extents<int, 4, 6>{});
    auto row = std::submdspan(matrix, 1, std::full_extent);
    auto column = std::submdspan(matrix, std::full_extent, 2);
    auto block = std::submdspan(matrix, std::pair{1, 3}, std::pair{2, 5});
    auto spaced = std::submdspan(matrix, std::range_slice{0, 4, 2}, std::full_extent);
    forge_example::require(row.extent(0) == 6 && row[0] == 7);
    forge_example::require(column.extent(0) == 4 && column[2] == 15);
    forge_example::require((block.extent(0) == 2 && block.extent(1) == 3 && block[1, 2] == 17));
    forge_example::require((spaced.extent(0) == 2 && spaced[1, 0] == 13));

    std::array<int, 64> cells{};
    cells.fill(1);
    std::mdspan grid(cells.data(), std::extents<int, 4, 4, 4>{});
    // Zero each boundary cell; eight interior cells remain unchanged.
    for (int z = 0; z < 4; ++z) {
        auto plane = std::submdspan(grid, z, std::full_extent, std::full_extent);
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x)
                if (z == 0 || z == 3 || y == 0 || y == 3 || x == 0 || x == 3)
                    plane[y, x] = 0;
    }
    int interior = 0;
    for (int value : cells) interior += value;
    forge_example::require(interior == 8);
    std::cout << "Slices and eight interior cells verified\n";
}
