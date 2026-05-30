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

class single_thread_context {
public:
    using scheduler = static_thread_pool::scheduler;

    single_thread_context() : __pool_(1) {}
    ~single_thread_context() noexcept = default;

    single_thread_context(const single_thread_context&) = delete;
    single_thread_context& operator=(const single_thread_context&) = delete;
    single_thread_context(single_thread_context&&) = delete;
    single_thread_context& operator=(single_thread_context&&) = delete;

    [[nodiscard]] scheduler get_scheduler() noexcept {
        return __pool_.get_scheduler();
    }

    void shutdown() noexcept {
        __pool_.shutdown();
    }

    void wait() noexcept {
        __pool_.wait();
    }

private:
    static_thread_pool __pool_;
};

} // namespace forge
