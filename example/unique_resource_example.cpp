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

#include <forge.hpp>
#include <memory>  // Standard library header, CMake handles backport automatically
#include <cstdio>
#include <iostream>
#include <vector>

void example_unique_resource() {
    std::cout << "=== std::unique_resource (zero-overhead) ===\n";

    // Using function pointer - zero overhead
    auto file1 = std::make_unique_resource_checked(
        fopen("test1.txt", "w"),
        nullptr,
        &fclose
    );

    if (file1.get() != nullptr) {
        fprintf(file1.get(), "Hello from unique_resource\n");
    }

    // Using lambda - zero overhead (stateless lambda can be optimized to function pointer)
    auto file2 = std::make_unique_resource_checked(
        fopen("test2.txt", "w"),
        nullptr,
        [](FILE* f) { if (f) fclose(f); }
    );

    // Note: file1 and file2 are different types!
    // decltype(file1) != decltype(file2)

    // Managing multiple resources
    auto files = std::unique_resource(
        std::vector<FILE*>{fopen("test3.txt", "w"), fopen("test4.txt", "w")},
        [](std::vector<FILE*>& vec) {
            for (auto* f : vec) if (f) fclose(f);
        }
    );

    for (size_t i = 0; i < files.get().size(); ++i) {
        if (files.get()[i]) {
            fprintf(files.get()[i], "File %zu\n", i);
        }
    }
}

int main() {
    example_unique_resource();

    return 0;
}
