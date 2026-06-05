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

namespace forge {

// ──────────────────────────────────────────────────────────────────────────
// system_context — process-lifetime shared thread pool (P2079 inspired)
//
// The singleton intentionally leaks instead of participating in static
// teardown. Call wait() explicitly when a program needs to drain accepted work.
//
// Usage:
//   auto& ctx = forge::system_context::get();
//   auto sch = ctx.get_scheduler();
// ──────────────────────────────────────────────────────────────────────────

class system_context {
public:
    // Get the global system_context singleton
    [[nodiscard]] static system_context& get() noexcept {
        static system_context* instance = new system_context;
        return *instance;
    }

    [[nodiscard]] static_thread_pool::scheduler get_scheduler() noexcept {
        return __pool_.get_scheduler();
    }

    [[nodiscard]] std::size_t thread_count() const noexcept {
        return __pool_.thread_count();
    }

    // Request graceful shutdown (does not block)
    void shutdown() noexcept {
        __pool_.shutdown();
    }

    // Wait for already accepted work to finish.
    void wait() noexcept {
        __pool_.wait();
    }

private:
    // Default: hardware_concurrency threads
    system_context() = default;
    ~system_context() = default;
    system_context(const system_context&) = delete;
    system_context& operator=(const system_context&) = delete;

    static_thread_pool __pool_;
};

[[nodiscard]] inline static_thread_pool::scheduler get_system_scheduler() noexcept {
    return system_context::get().get_scheduler();
}

} // namespace forge
