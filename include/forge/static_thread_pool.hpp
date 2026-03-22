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

#include <execution>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace forge {

// Forward declarations
class static_thread_pool;

namespace __pool_detail {

struct __env {
    static_thread_pool* pool;
};

struct __sender {
    using sender_concept = std::execution::sender_t;
    static_thread_pool* pool;

    friend auto tag_invoke(std::execution::get_completion_signatures_t,
                           const __sender&, auto) noexcept
        -> std::execution::completion_signatures<std::execution::set_value_t()> {
        return {};
    }

    friend auto tag_invoke(std::execution::get_env_t,
                           const __sender& self) noexcept -> __env {
        return __env{self.pool};
    }
};

template<class R>
struct __op {
    using operation_state_concept = std::execution::operation_state_t;
    __op(__op&&) = delete;
    __op& operator=(__op&&) = delete;
    __op(const __op&) = delete;
    __op& operator=(const __op&) = delete;
    __op(R r, static_thread_pool* p) : __rcvr(std::move(r)), __pool(p) {}

    R __rcvr;
    static_thread_pool* __pool;
};

} // namespace __pool_detail

class static_thread_pool {
public:
    using scheduler_concept_tag = std::execution::scheduler_t;

    struct scheduler {
        using scheduler_concept = std::execution::scheduler_t;
        bool operator==(const scheduler&) const noexcept = default;

        friend auto tag_invoke(std::execution::schedule_t,
                               const scheduler& self) noexcept
            -> __pool_detail::__sender {
            return __pool_detail::__sender{self.__pool};
        }

        static_thread_pool* __pool; // public for simplicity
    };

    explicit static_thread_pool(
        std::size_t thread_count = std::thread::hardware_concurrency())
        : __stop_(false), __active_(0)
    {
        if (thread_count == 0) thread_count = 1;
        __threads_.reserve(thread_count);
        for (std::size_t i = 0; i < thread_count; ++i)
            __threads_.emplace_back([this] { __run(); });
    }

    ~static_thread_pool() noexcept {
        shutdown();
        for (auto& t : __threads_)
            if (t.joinable()) t.join();
    }

    static_thread_pool(const static_thread_pool&) = delete;
    static_thread_pool& operator=(const static_thread_pool&) = delete;
    static_thread_pool(static_thread_pool&&) = delete;
    static_thread_pool& operator=(static_thread_pool&&) = delete;

    void shutdown() noexcept {
        std::lock_guard lk{__mtx_};
        __stop_ = true;
        __cv_.notify_all();
    }

    void wait() noexcept {
        std::unique_lock lk{__mtx_};
        __cv_wait_.wait(lk, [this] {
            return __queue_.empty() && __active_ == 0;
        });
    }

    [[nodiscard]] scheduler get_scheduler() noexcept {
        return scheduler{this};
    }

    [[nodiscard]] std::size_t thread_count() const noexcept {
        return __threads_.size();
    }

    void __submit(std::function<void()> task) {
        std::lock_guard lk{__mtx_};
        if (!__stop_) {
            __queue_.push_back(std::move(task));
            ++__active_;
            __cv_.notify_one();
        }
    }

private:
    void __run() noexcept {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock lk{__mtx_};
                __cv_.wait(lk, [this] { return !__queue_.empty() || __stop_; });
                if (__queue_.empty()) break;
                task = std::move(__queue_.front());
                __queue_.pop_front();
            }
            try { task(); } catch (...) {}
            {
                std::lock_guard lk{__mtx_};
                --__active_;
                if (__queue_.empty() && __active_ == 0)
                    __cv_wait_.notify_all();
            }
        }
    }

    std::mutex __mtx_;
    std::condition_variable __cv_;
    std::condition_variable __cv_wait_;
    std::deque<std::function<void()>> __queue_;
    std::vector<std::thread> __threads_;
    bool __stop_;
    std::size_t __active_;
};

// Define __op start after static_thread_pool is complete
namespace __pool_detail {

template<class R>
inline void tag_invoke(std::execution::start_t, __op<R>& self) noexcept {
    self.__pool->__submit([&self]() noexcept {
        std::execution::set_value(std::move(self.__rcvr));
    });
}

template<std::execution::receiver R>
inline auto tag_invoke(std::execution::connect_t, __sender self, R r) {
    return __op<R>{std::move(r), self.pool};
}

} // namespace __pool_detail

} // namespace forge
