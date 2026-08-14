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
#include <latch>
#include <thread>
#include <utility>

namespace {

class loop_worker {
public:
    loop_worker()
        : worker_([this] {
              id_ = std::this_thread::get_id();
              started_.count_down();
              loop_.run();
          }) {
        started_.wait();
    }

    ~loop_worker() {
        loop_.finish();
        worker_.join();
    }

    loop_worker(const loop_worker&) = delete;
    auto operator=(const loop_worker&) -> loop_worker& = delete;

    [[nodiscard]] auto scheduler() noexcept {
        return loop_.get_scheduler();
    }

    [[nodiscard]] auto id() const noexcept -> std::thread::id {
        return id_;
    }

private:
    std::execution::run_loop loop_;
    std::latch started_{1};
    std::thread::id id_{};
    std::thread worker_;
};

struct start_env {
    std::execution::run_loop::scheduler scheduler;

    friend auto tag_invoke(
        std::execution::get_start_scheduler_t,
        const start_env& self) noexcept -> std::execution::run_loop::scheduler {
        return self.scheduler;
    }
};

struct completion_state {
    std::latch done{1};
    std::thread::id source_thread{};
    std::thread::id completion_thread{};
    int value = 0;
};

struct receiver {
    using receiver_concept = std::execution::receiver_t;

    completion_state* state;
    start_env env;

    void set_value(int value) && noexcept {
        state->completion_thread = std::this_thread::get_id();
        state->value = value;
        state->done.count_down();
    }

    template<class Error>
    void set_error(Error&&) && noexcept {
        state->value = -1;
        state->done.count_down();
    }

    void set_stopped() && noexcept {
        state->value = -2;
        state->done.count_down();
    }

    auto get_env() const noexcept -> start_env {
        return env;
    }
};

} // namespace

int main() {
    loop_worker source;
    loop_worker destination;
    completion_state state;

    auto sender = std::execution::continues_on(
        std::execution::just(40), source.scheduler())
        | std::execution::then([&](int value) {
              state.source_thread = std::this_thread::get_id();
              return value + 2;
          })
        | std::execution::affine;

    auto operation = std::execution::connect(
        std::move(sender), receiver{&state, start_env{destination.scheduler()}});
    std::execution::start(operation);
    state.done.wait();

    forge_example::require(state.value == 42);
    forge_example::require(state.source_thread == source.id());
    forge_example::require(state.completion_thread == destination.id());
    forge_example::require(source.id() != destination.id());

    std::cout << "affine_result=" << state.value << '\n';
    return 0;
}
