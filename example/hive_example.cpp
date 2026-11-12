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

// Demonstrates: Quick start with insertion and erasure
// Key APIs: insert, erase
// Requires: injected backport or complete native <hive>; C++23
// See also: docs/backports/hive.md

#include <hive>
#include <iostream>
#include "example_support.hpp"

int main() {
    std::hive<int> values;
    values.insert(10);
    auto removed = values.insert(20);
    auto retained = values.insert(30);
    values.erase(removed);
    values.insert(40);

    forge_example::require(*retained == 30 && values.size() == 3,
                           "retained element and size");
    // Erasing another element preserves this iterator.
    // New elements may occupy holes; traversal is not insertion order.
    // The printed sequence is illustrative, never an ordering contract.
    // Fetch end() again after insertion rather than caching it.
    for (int value : values) {
        std::cout << value << '\n';
    }
}
