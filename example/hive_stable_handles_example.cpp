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

// Demonstrates: Keep emitter handles while short-lived particles expire
// Key APIs: emplace, erase_if
// Requires: injected backport or complete native <hive>; C++23
// See also: docs/backports/hive.md

#include <hive>
#include <array>
#include <cstddef>
#include <memory>
#include "example_support.hpp"

struct Spark { int id; int expires; };

int main() {
    using Pool = std::hive<Spark>;
    Pool sparks;
    struct Handle { int id; Spark* pointer; Pool::iterator iterator; };
    std::array<Handle, 2> emitters;
    for (int i = 0; i < 2; ++i) {
        auto it = sparks.emplace(700 + i, 100);
        emitters[i] = {700 + i, std::addressof(*it), it};
    }
    std::size_t expected = emitters.size();
    for (int frame = 0; frame < 6; ++frame) {
        for (int j = 0; j < 3; ++j) {
            sparks.emplace(frame * 10 + j, frame + 1);
            ++expected;
        }
        // Reacquire both range endpoints after insertion; never cache end().
        std::size_t expired = 0;
        for (const auto& spark : sparks) expired += spark.expires <= frame;
        const auto removed = std::erase_if(sparks, [frame](const Spark& spark) {
            return spark.expires <= frame;
        });
        forge_example::require(removed == expired, "expired particle count");
        expected -= expired;
        forge_example::require(sparks.size() == expected, "independent live count");
    }
    for (const auto& saved : emitters) {
        forge_example::require(saved.pointer->id == saved.id &&
                               saved.iterator->id == saved.id &&
                               std::addressof(*saved.iterator) == saved.pointer,
                               "emitter handles survive churn");
    }
}
