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

// Demonstrates: Transfer stage results without losing downstream pointers
// Key APIs: splice, get_iterator, is_within_hard_limits
// Requires: injected backport or complete native <hive>; C++23
// See also: docs/backports/hive.md

#include <hive>
#include <iostream>
#include <memory>
#include <stdexcept>
#include "example_support.hpp"

struct Result { int id; };

int main() {
    using Results = std::hive<Result>;
    Results accumulated;
    Results stage;
    accumulated.emplace(410);
    Result* saved = std::addressof(*stage.emplace(620));
    const auto total = accumulated.size() + stage.size();
    // Default allocators compare equal. Do not retain either end() iterator.
    accumulated.splice(stage);
    forge_example::require(stage.empty() && accumulated.size() == total &&
                           saved->id == 620 &&
                           std::addressof(*accumulated.get_iterator(saved)) == saved,
                           "stage pointer now belongs to accumulated results");

    const std::hive_limits narrow{3, 3};
    const std::hive_limits wide{9, 9};
    if (!Results::is_within_hard_limits(narrow) ||
        !Results::is_within_hard_limits(wide)) {
        std::cout << "Incompatible-limit demonstration unavailable\n";
        return 0;
    }
    Results destination(narrow);
    Results oversized_stage(wide);
    Result* old = std::addressof(*destination.emplace(730));
    Result* incoming = std::addressof(*oversized_stage.emplace(840));
    const auto destination_count = destination.size();
    const auto stage_count = oversized_stage.size();
    bool rejected = false;
    try {
        destination.splice(oversized_stage);
    } catch (const std::length_error&) {
        rejected = true;
    }
    // The source active block has capacity 9, outside destination's [3,3].
    // Standard splice failure has no effects on either hive.
    forge_example::require(rejected && destination.size() == destination_count &&
                           oversized_stage.size() == stage_count &&
                           old->id == 730 && incoming->id == 840 &&
                           std::addressof(*destination.get_iterator(old)) == old &&
                           std::addressof(*oversized_stage.get_iterator(incoming)) == incoming,
                           "incompatible splice leaves both containers unchanged");
}
