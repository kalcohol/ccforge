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

#include "async_scope.hpp"
#include "resource_policy.hpp"
#include "runtime_context.hpp"

#include <chrono>
#include <cstddef>
#include <execution>
#include <memory_resource>
#include <optional>
#include <thread>
#include <utility>

namespace forge {

struct resource_context_options {
    std::size_t thread_count = std::thread::hardware_concurrency();
    std::optional<std::size_t> queue_capacity = std::nullopt;
    std::pmr::memory_resource* memory = default_memory_resource();
};

class resource_context {
public:
    using scheduler = runtime_context::scheduler;

    explicit resource_context(
        std::size_t thread_count = std::thread::hardware_concurrency())
        : resource_context(resource_context_options{.thread_count = thread_count})
    {}

    explicit resource_context(resource_context_options options)
        : runtime_(runtime_context_options{
              .thread_count = options.thread_count,
              .queue_capacity = options.queue_capacity,
              .memory = options.memory,
          })
        , scope_(async_scope_options{.memory = options.memory})
    {}

    ~resource_context() noexcept {
        shutdown();
        wait();
    }

    resource_context(const resource_context&) = delete;
    resource_context& operator=(const resource_context&) = delete;
    resource_context(resource_context&&) = delete;
    resource_context& operator=(resource_context&&) = delete;

    [[nodiscard]] scheduler get_scheduler() noexcept {
        return runtime_.get_scheduler();
    }

    [[nodiscard]] std::size_t thread_count() const noexcept {
        return runtime_.thread_count();
    }

    template<class Rep, class Period>
    [[nodiscard]] auto schedule_after(std::chrono::duration<Rep, Period> delay) {
        return runtime_.schedule_after(delay);
    }

    template<class Clock, class Duration>
    [[nodiscard]] auto schedule_at(std::chrono::time_point<Clock, Duration> time) {
        return runtime_.schedule_at(time);
    }

    [[nodiscard]] async_scope& scope() noexcept {
        return scope_;
    }

    template<std::execution::sender S>
    bool spawn(S&& sender) {
        return scope_.spawn(static_cast<S&&>(sender));
    }

    void request_stop() noexcept {
        scope_.request_stop();
    }

    void close() noexcept {
        scope_.close();
    }

    void shutdown() noexcept {
        scope_.close();
        scope_.request_stop();
        runtime_.shutdown();
    }

    void wait() noexcept {
        runtime_.wait();
        scope_.wait();
        runtime_.wait();
    }

private:
    runtime_context runtime_;
    async_scope scope_;
};

} // namespace forge
