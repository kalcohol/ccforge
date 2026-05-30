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
#include <optional>
#include <thread>
#include <vector>

namespace forge {

// Forward declarations
class static_thread_pool;

struct static_thread_pool_options {
    std::size_t thread_count = std::thread::hardware_concurrency();
    std::optional<std::size_t> queue_capacity = std::nullopt;
};

namespace __pool_detail {

struct __env {
    static_thread_pool* pool;
};

template<class R>
struct __op;

struct __sender {
    using sender_concept = std::execution::sender_t;
    static_thread_pool* pool;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<
            std::execution::set_value_t(),
            std::execution::set_stopped_t()> {
        return {};
    }

    auto get_env() const noexcept -> __env {
        return __env{pool};
    }

    template<std::execution::receiver R>
    auto connect(R r) && -> __op<R>;

    template<std::execution::receiver R>
    auto connect(R r) const& -> __op<R>;
};

template<class R>
struct __op {
    using operation_state_concept = std::execution::operation_state_t;
    __op(__op&&) = delete;
    __op& operator=(__op&&) = delete;
    __op(const __op&) = delete;
    __op& operator=(const __op&) = delete;
    __op(R r, static_thread_pool* p) : __rcvr(std::move(r)), __pool(p) {}

    void start() & noexcept;

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

        auto schedule() const noexcept -> __pool_detail::__sender {
            return __pool_detail::__sender{__pool};
        }

        static_thread_pool* __pool; // public for simplicity
    };

    explicit static_thread_pool(
        std::size_t thread_count = std::thread::hardware_concurrency())
        : static_thread_pool(static_thread_pool_options{thread_count, std::nullopt})
    {}

    explicit static_thread_pool(static_thread_pool_options options)
        : __queue_capacity_(options.queue_capacity), __stop_(false), __active_(0)
    {
        auto thread_count = options.thread_count;
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

    bool __submit(std::function<void()> task) {
        std::lock_guard lk{__mtx_};
        if (__stop_) {
            return false;
        }
        if (__queue_capacity_ && __queue_.size() >= *__queue_capacity_) {
            return false;
        }
        __queue_.push_back(std::move(task));
        ++__active_;
        __cv_.notify_one();
        return true;
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
    std::optional<std::size_t> __queue_capacity_;
    std::vector<std::thread> __threads_;
    bool __stop_;
    std::size_t __active_;
};

namespace __pool_detail {

inline auto tag_invoke(
    std::execution::get_completion_scheduler_t<std::execution::set_value_t>,
    const __env& self) noexcept -> static_thread_pool::scheduler {
    return static_thread_pool::scheduler{self.pool};
}

} // namespace __pool_detail

// Define __op start after static_thread_pool is complete
namespace __pool_detail {

template<class R>
bool __stop_requested(const R& rcvr) noexcept {
    if constexpr (requires {
                      std::execution::get_stop_token(
                          std::execution::get_env(rcvr));
                  }) {
        return std::execution::get_stop_token(
            std::execution::get_env(rcvr)).stop_requested();
    } else {
        return false;
    }
}

template<class R>
inline void __op<R>::start() & noexcept {
    if (__stop_requested(__rcvr)) {
        std::execution::set_stopped(std::move(__rcvr));
        return;
    }

    if (!__pool->__submit([this]() noexcept {
        std::execution::set_value(std::move(__rcvr));
    })) {
        std::execution::set_stopped(std::move(__rcvr));
    }
}

template<std::execution::receiver R>
inline auto __sender::connect(R r) && -> __op<R> {
    return __op<R>{std::move(r), pool};
}

template<std::execution::receiver R>
inline auto __sender::connect(R r) const& -> __op<R> {
    return __op<R>{std::move(r), pool};
}

} // namespace __pool_detail

} // namespace forge
