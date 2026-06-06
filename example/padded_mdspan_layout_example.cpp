// MIT License
//
// Copyright (c) 2026 CC Forge Project
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

#include <mdspan>
#include <array>
#include <type_traits>
#include "example_support.hpp"

int main() {
    using extents_t = std::extents<int, 3, 4>;

    std::array<int, 32> left_storage{};
    for (std::size_t i = 0; i < left_storage.size(); ++i) {
        left_storage[i] = static_cast<int>(i);
    }

    using left_mapping_t = std::layout_left_padded<8>::mapping<extents_t>;
    std::mdspan<int, extents_t, std::layout_left_padded<8>> left(
        left_storage.data(),
        left_mapping_t{extents_t{}, 8});

    forge_example::require(left.mapping().stride(0) == 1);
    forge_example::require(left.mapping().stride(1) == 8);
    forge_example::require(left[2, 3] == 26);

    auto full_left = std::submdspan(left, std::full_extent, std::full_extent);
    static_assert(std::is_same_v<
                  decltype(full_left)::layout_type,
                  std::layout_left_padded<8>>);
    forge_example::require(full_left.mapping().stride(1) == 8);

    std::array<int, 32> right_storage{};
    for (std::size_t i = 0; i < right_storage.size(); ++i) {
        right_storage[i] = static_cast<int>(i);
    }

    using right_mapping_t = std::layout_right_padded<8>::mapping<extents_t>;
    std::mdspan<int, extents_t, std::layout_right_padded<8>> right(
        right_storage.data(),
        right_mapping_t{extents_t{}, 8});

    forge_example::require(right.mapping().stride(0) == 8);
    forge_example::require(right.mapping().stride(1) == 1);
    forge_example::require(right[2, 3] == 19);
}
