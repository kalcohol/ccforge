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

#include <iostream>
#include <optional>
#include <tuple>

namespace {

struct start_env {
    std::execution::inline_scheduler scheduler;

    friend auto tag_invoke(
        std::execution::get_start_scheduler_t,
        const start_env& self) noexcept -> std::execution::inline_scheduler {
        return self.scheduler;
    }
};

struct int_receiver {
    using receiver_concept = std::execution::receiver_t;

    int* value = nullptr;
    start_env env{};

    void set_value(int v) && noexcept {
        *value = v;
    }

    template<class Error>
    void set_error(Error&&) && noexcept {
        *value = -1;
    }

    void set_stopped() && noexcept {
        *value = -2;
    }

    auto get_env() const noexcept -> start_env {
        return env;
    }
};

} // namespace

int main() {
    std::execution::inline_scheduler scheduler;

    int first_form_value = 0;
    auto first_form = std::execution::on(
        scheduler,
        std::execution::just(7) | std::execution::then([](int value) {
            return value + 1;
        }));
    auto first_op = std::execution::connect(
        std::move(first_form),
        int_receiver{&first_form_value, start_env{scheduler}});
    std::execution::start(first_op);

    auto closure_form = std::execution::on(
        std::execution::schedule(scheduler),
        scheduler,
            [](auto shifted) {
                return std::move(shifted)
                    | std::execution::then([] {
                          return 21;
                      });
            });
    auto closure_result = std::this_thread::sync_wait(std::move(closure_form));

    std::cout << "on_start=" << first_form_value
              << ", on_closure=" << std::get<0>(*closure_result) << '\n';
    return 0;
}
