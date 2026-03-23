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

#if __cpp_impl_coroutine >= 201902L
#include <coroutine>
#include <forge/task.hpp>
#include <iostream>
using exec_env = std::execution::empty_env;
struct demo { struct promise_type : std::execution::with_awaitable_senders<promise_type> { demo get_return_object() { return {}; } std::suspend_never initial_suspend() noexcept { return {}; } std::suspend_never final_suspend() noexcept { return {}; } void return_void() {} void unhandled_exception() { std::terminate(); } }; };
demo run() { auto [v] = co_await (std::execution::just(20) | std::execution::then([](int x) { return x + 22; })); std::cout << "execution coro value: " << v << '\n'; }
int main() { run(); return 0; }
#else
#include <execution>
#include <iostream>
int main() { std::cout << "C++20 coroutines required\n"; (void)sizeof(std::execution::empty_env); return 0; }
#endif
