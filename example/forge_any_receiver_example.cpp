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

#include <forge/any_receiver.hpp>
#include <iostream>
struct print_receiver { using receiver_concept = std::execution::receiver_t; int* out{};
    friend void tag_invoke(std::execution::set_value_t, print_receiver&& r, int v) noexcept { *r.out = v; }
    friend void tag_invoke(std::execution::set_error_t, print_receiver&&, std::exception_ptr) noexcept {}
    friend void tag_invoke(std::execution::set_stopped_t, print_receiver&&) noexcept {}
    friend std::execution::empty_env tag_invoke(std::execution::get_env_t, const print_receiver&) noexcept { return {}; } };
int main() {
    using cs_int = std::execution::completion_signatures<std::execution::set_value_t(int), std::execution::set_error_t(std::exception_ptr), std::execution::set_stopped_t()>;
    int captured = 0; forge::any_receiver_of<cs_int> erased = print_receiver{&captured}; std::execution::set_value(std::move(erased), 7);
    std::cout << "any_receiver value=" << captured << '\n';
    return 0; }
