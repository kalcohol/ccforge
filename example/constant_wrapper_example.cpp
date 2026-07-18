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

#include <utility>

#include <cstddef>
#include <type_traits>

#include "example_support.hpp"

template<auto V>
constexpr auto doubled(std::constant_wrapper<V>) {
    return std::cw<(V * 2)>;
}

template<auto V>
struct external_constant {
    static constexpr auto value = V;
};

struct lookup {
    int values[3];

    constexpr const int& operator[](std::size_t index) const noexcept {
        return values[index];
    }
};

constexpr int plus_one(int value) noexcept {
    return value + 1;
}

int main() {
    constexpr auto rows = std::cw<3zu>;
    constexpr auto cols = std::cw<4zu>;
    constexpr auto elements = rows * external_constant<4zu>{};
    constexpr auto next = std::cw<&plus_one>(rows);
    constexpr auto selected = std::cw<lookup{{2, 4, 8}}>[std::cw<2zu>];

    static_assert(std::is_same_v<
                  decltype(elements),
                  const std::constant_wrapper<12zu>>);
    static_assert(doubled(rows) == std::cw<6zu>);
    static_assert(std::is_same_v<
                  decltype(next),
                  const std::constant_wrapper<4>>);
    static_assert(std::is_same_v<
                  decltype(selected),
                  const std::constant_wrapper<8>>);

    forge_example::require(elements.value == 12zu);
    forge_example::require(next.value == 4);
    forge_example::require(selected.value == 8);
}
