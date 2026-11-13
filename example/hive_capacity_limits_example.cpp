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

// Demonstrates: Plan connection bursts and change block limits
// Key APIs: hive_limits, reserve, trim_capacity, reshape
// Requires: injected backport or complete native <hive>; C++23
// See also: docs/backports/hive.md

#include <hive>
#include <iostream>
#include <memory>
#include <set>
#include "example_support.hpp"

struct Connection { int id; };

int main() {
    using Table = std::hive<Connection>;
    const std::hive_limits initial{4, 16};
    const std::hive_limits larger{8, 32};
    if (!Table::is_within_hard_limits(initial) ||
        !Table::is_within_hard_limits(larger)) {
        std::cout << "Requested connection limits unavailable\n";
    return 77;
        return 0;
    }
    Table connections(initial);
    auto anchor = connections.emplace(91);
    Connection* saved = std::addressof(*anchor);
    connections.reserve(48);
    forge_example::require(connections.capacity() >= 48 && saved->id == 91 &&
                           std::addressof(*anchor) == saved, "reserve preserves handles");
    for (int id = 0; id < 40; ++id) connections.emplace(id);
    std::erase_if(connections, [](const Connection& c) { return c.id < 30; });
    connections.trim_capacity();
    forge_example::require(connections.capacity() >= connections.size(), "capacity covers size");
    std::multiset<int> before;
    for (const auto& c : connections) before.insert(c.id);
    const auto count = connections.size();
    connections.reshape(larger);
    // Reshape can invalidate every handle: collect values again from the hive.
    std::multiset<int> after;
    for (const auto& c : connections) after.insert(c.id);
    const auto limits = connections.block_capacity_limits();
    forge_example::require(limits.min == larger.min && limits.max == larger.max &&
                           connections.size() == count && before == after,
                           "reshape preserves ids and installs limits");
    forge_example::require(connections.capacity() >= connections.size(), "reshaped capacity");
    connections.clear();
    connections.trim_capacity();
    // Empty means no active blocks; trim releases all reserved blocks.
    forge_example::require(connections.capacity() == 0, "empty trimmed capacity");
}
