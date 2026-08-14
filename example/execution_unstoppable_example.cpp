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

#include <execution>

#include "example_support.hpp"

#include <iostream>
#include <tuple>

namespace {

auto stop_env(std::inplace_stop_token token) {
    return std::execution::make_env(
        std::execution::make_prop(std::execution::get_stop_token_t{}, token));
}

} // namespace

int main() {
    std::inplace_stop_source source;
    source.request_stop();

    auto observes_outer_stop = std::execution::write_env(
        std::execution::read_env(std::execution::get_stop_token)
            | std::execution::then([](auto token) {
                  return token.stop_requested();
              }),
        stop_env(source.get_token()));

    auto shielded = std::execution::write_env(
        std::execution::unstoppable(
            std::execution::read_env(std::execution::get_stop_token)
                | std::execution::then([](auto token) {
                      return token.stop_possible();
                  })),
        stop_env(source.get_token()));

    auto outer = std::this_thread::sync_wait(std::move(observes_outer_stop));
    auto inner = std::this_thread::sync_wait(std::move(shielded));
    forge_example::require(outer.has_value());
    forge_example::require(inner.has_value());
    forge_example::require(std::get<0>(*outer));
    forge_example::require(!std::get<0>(*inner));

    std::cout << "outer_stop_requested=" << std::get<0>(*outer)
              << ", inner_stop_possible=" << std::get<0>(*inner) << '\n';
    return 0;
}
