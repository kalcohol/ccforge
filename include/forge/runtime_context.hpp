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

#pragma once

#include "static_thread_pool.hpp"
#include "timer_context.hpp"

#include <chrono>
#include <cstddef>
#include <memory_resource>
#include <optional>
#include <thread>

namespace forge {

struct runtime_context_options {
    std::size_t thread_count = std::thread::hardware_concurrency();
    std::optional<std::size_t> queue_capacity = std::nullopt;
    std::pmr::memory_resource* memory = default_memory_resource();
};

class runtime_context {
public:
    using scheduler = static_thread_pool::scheduler;

    explicit runtime_context(
        std::size_t thread_count = std::thread::hardware_concurrency())
        : runtime_context(runtime_context_options{.thread_count = thread_count})
    {}

    explicit runtime_context(runtime_context_options options)
        : pool_(static_thread_pool_options{
              .thread_count = options.thread_count,
              .queue_capacity = options.queue_capacity,
              .memory = options.memory,
          })
        , timers_(timer_context_options{.memory = options.memory})
    {}

    ~runtime_context() noexcept {
        shutdown();
        wait();
    }

    runtime_context(const runtime_context&) = delete;
    runtime_context& operator=(const runtime_context&) = delete;
    runtime_context(runtime_context&&) = delete;
    runtime_context& operator=(runtime_context&&) = delete;

    [[nodiscard]] scheduler get_scheduler() noexcept {
        return pool_.get_scheduler();
    }

    [[nodiscard]] std::size_t thread_count() const noexcept {
        return pool_.thread_count();
    }

    template<class Rep, class Period>
    [[nodiscard]] auto schedule_after(std::chrono::duration<Rep, Period> delay) {
        return timers_.schedule_after(delay);
    }

    template<class Clock, class Duration>
    [[nodiscard]] auto schedule_at(std::chrono::time_point<Clock, Duration> time) {
        return timers_.schedule_at(time);
    }

    void shutdown() noexcept {
        timers_.shutdown();
        pool_.shutdown();
    }

    void wait() noexcept {
        pool_.wait();
        timers_.wait();
        pool_.wait();
    }

private:
    static_thread_pool pool_;
    timer_context timers_;
};

} // namespace forge
