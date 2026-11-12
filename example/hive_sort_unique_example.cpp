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

// Demonstrates: Order and deduplicate accepted telemetry keys
// Key APIs: sort, unique
// Requires: injected backport or complete native <hive>; C++23
// See also: docs/backports/hive.md

#include <hive>
#include <initializer_list>
#include <algorithm>
#include <set>
#include <utility>
#include "example_support.hpp"

struct Sample { int sensor; int tick; int reading; };

int main() {
    std::hive<Sample> samples;
    for (int sensor : {6, 2, 9}) {
        for (int tick : {12, 4, 12, 7}) samples.emplace(sensor, tick, sensor + tick);
    }
    samples.emplace(6, 99, -1);
    std::erase_if(samples, [](const Sample& s) { return s.reading < 0; });
    const auto less = [](const Sample& a, const Sample& b) {
        return std::pair{a.sensor, a.tick} < std::pair{b.sensor, b.tick};
    };
    const auto equivalent = [](const Sample& a, const Sample& b) {
        return a.sensor == b.sensor && a.tick == b.tick;
    };
    std::set<std::pair<int, int>> keys;
    for (const auto& s : samples) keys.emplace(s.sensor, s.tick);
    const auto count = samples.size();
    const auto duplicates = count - keys.size();
    // Sort is unstable and can invalidate handles; retain no handles here.
    // Project D1: on exception the hive remains valid, size unchanged, no leaks;
    // order and individual values are unspecified. No exception demo is needed.
    samples.sort(less);
    forge_example::require(samples.size() == count &&
                           std::is_sorted(samples.begin(), samples.end(), less),
                           "telemetry sorted by key");
    const auto removed = samples.unique(equivalent);
    // Unique retains the first element of each adjacent equivalent run.
    forge_example::require(removed == duplicates &&
                           std::adjacent_find(samples.begin(), samples.end(), equivalent) == samples.end(),
                           "adjacent duplicates removed");
}
