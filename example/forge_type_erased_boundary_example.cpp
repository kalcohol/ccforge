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

#include <forge/any_scheduler.hpp>
#include <forge/erased_sender.hpp>
#include <forge/static_thread_pool.hpp>

#include <execution>
#include "example_support.hpp"
#include <exception>
#include <tuple>

namespace {

using plugin_result = std::execution::completion_signatures<
    std::execution::set_value_t(int),
    std::execution::set_error_t(std::exception_ptr),
    std::execution::set_stopped_t()>;

using plugin_operation = forge::erased_sender<plugin_result>;

plugin_operation make_plugin_operation(forge::any_scheduler scheduler, int value) {
    return plugin_operation{
        std::execution::schedule(std::move(scheduler))
        | std::execution::then([value] noexcept {
            return value * 3;
        })};
}

int run_plugin(plugin_operation op) {
    auto result = std::this_thread::sync_wait(std::move(op));
    forge_example::require(result.has_value());
    return std::get<0>(*result);
}

} // namespace

int main() {
    forge::static_thread_pool pool{2};

    forge::any_scheduler selected_runtime{pool.get_scheduler()};
    plugin_operation op = make_plugin_operation(std::move(selected_runtime), 14);

    forge_example::require(run_plugin(std::move(op)) == 42);
}
