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

// Demonstrates: Recover pool iterators from callback pointers
// Key APIs: get_iterator, erase
// Requires: injected backport or complete native <hive>; C++23
// See also: docs/backports/hive.md

#include <hive>
#include <initializer_list>
#include <memory>
#include <set>
#include <vector>
#include "example_support.hpp"

struct Entity { int id; };

void report(const std::hive<Entity>& pool, const std::vector<Entity*>& callbacks) {
    for (const Entity* pointer : callbacks) {
        auto it = pool.get_iterator(pointer);
        forge_example::require(std::addressof(*it) == pointer, "const callback handle");
    }
}

int main() {
    std::hive<Entity> pool;
    std::vector<Entity*> callbacks;
    std::set<int> expected;
    for (int id : {81, 34, 67, 52}) {
        callbacks.push_back(std::addressof(*pool.emplace(id)));
        expected.insert(id);
    }
    report(pool, callbacks);
    const auto& view = pool;
    for (Entity* pointer : callbacks) {
        auto it = pool.get_iterator(pointer);
        auto const_it = view.get_iterator(static_cast<const Entity*>(pointer));
        forge_example::require(std::addressof(*it) == pointer && it == const_it,
                               "mutable and const recovery agree");
    }
    // Every input comes from this pool and still denotes a live element.
    Entity* retiring = callbacks.back();
    const auto count = pool.size();
    expected.erase(retiring->id);
    callbacks.pop_back();
    pool.erase(pool.get_iterator(retiring));
    // Do not use retiring again, including as input to get_iterator.
    std::set<int> actual;
    for (const auto& entity : pool) actual.insert(entity.id);
    forge_example::require(pool.size() == count - 1 && actual == expected,
                           "callback removal matches mirror");
    report(pool, callbacks);
}
