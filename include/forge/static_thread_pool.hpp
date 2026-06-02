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

#include "resource_policy.hpp"

#include <execution>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <new>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace forge {

// Forward declarations
class static_thread_pool;

struct static_thread_pool_options {
    std::size_t thread_count = std::thread::hardware_concurrency();
    std::optional<std::size_t> queue_capacity = std::nullopt;
    std::pmr::memory_resource* memory = default_memory_resource();
};

namespace __pool_detail {

struct __env {
    static_thread_pool* pool;
};

class __task {
public:
    __task() noexcept = default;

    template<class F>
    static auto make(F&& f, std::pmr::memory_resource* memory) -> __task {
        using model_t = __model<std::decay_t<F>>;
        memory = normalize_memory_resource(memory);
        void* storage = memory->allocate(sizeof(model_t), alignof(model_t));
        try {
            ::new (storage) model_t(static_cast<F&&>(f));
        } catch (...) {
            memory->deallocate(storage, sizeof(model_t), alignof(model_t));
            throw;
        }
        return __task{storage, memory, &__model_ops<std::decay_t<F>>};
    }

    ~__task() noexcept {
        reset();
    }

    __task(__task&& other) noexcept
        : ptr_(std::exchange(other.ptr_, nullptr))
        , memory_(std::exchange(other.memory_, nullptr))
        , ops_(std::exchange(other.ops_, nullptr))
    {}

    auto operator=(__task&& other) noexcept -> __task& {
        if (this == &other) {
            return *this;
        }
        reset();
        ptr_ = std::exchange(other.ptr_, nullptr);
        memory_ = std::exchange(other.memory_, nullptr);
        ops_ = std::exchange(other.ops_, nullptr);
        return *this;
    }

    __task(const __task&) = delete;
    auto operator=(const __task&) -> __task& = delete;

    explicit operator bool() const noexcept {
        return ptr_ != nullptr;
    }

    void operator()() {
        ops_->invoke(ptr_);
    }

    void reset() noexcept {
        if (!ptr_) {
            return;
        }
        ops_->destroy(ptr_, memory_);
        ptr_ = nullptr;
        memory_ = nullptr;
        ops_ = nullptr;
    }

private:
    struct __ops {
        void (*invoke)(void*);
        void (*destroy)(void*, std::pmr::memory_resource*) noexcept;
    };

    template<class F>
    struct __model {
        explicit __model(F&& f)
            : callable(static_cast<F&&>(f)) {}

        F callable;
    };

    template<class F>
    static inline const __ops __model_ops{
        [](void* ptr) {
            std::invoke(static_cast<__model<F>*>(ptr)->callable);
        },
        [](void* ptr, std::pmr::memory_resource* memory) noexcept {
            auto* model = static_cast<__model<F>*>(ptr);
            std::destroy_at(model);
            memory->deallocate(model, sizeof(__model<F>), alignof(__model<F>));
        }};

    __task(void* ptr, std::pmr::memory_resource* memory, const __ops* ops) noexcept
        : ptr_(ptr)
        , memory_(memory)
        , ops_(ops)
    {}

    void* ptr_ = nullptr;
    std::pmr::memory_resource* memory_ = nullptr;
    const __ops* ops_ = nullptr;
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
        : __memory_(normalize_memory_resource(options.memory))
        , __queue_(__memory_)
        , __queue_capacity_(options.queue_capacity)
        , __stop_(false)
        , __active_(0)
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
        if (__called_from_worker()) {
            return;
        }
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

    template<class F>
    bool __submit(F&& task) {
        std::lock_guard lk{__mtx_};
        if (__stop_) {
            return false;
        }
        if (__queue_capacity_ && __queue_.size() >= *__queue_capacity_) {
            return false;
        }
        __queue_.push_back(__pool_detail::__task::make(
            static_cast<F&&>(task),
            __memory_));
        ++__active_;
        __cv_.notify_one();
        return true;
    }

private:
    [[nodiscard]] bool __called_from_worker() const noexcept {
        const auto current = std::this_thread::get_id();
        for (const auto& thread : __threads_) {
            if (thread.get_id() == current) {
                return true;
            }
        }
        return false;
    }

    void __run() noexcept {
        while (true) {
            __pool_detail::__task task;
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
    std::pmr::memory_resource* __memory_;
    std::pmr::deque<__pool_detail::__task> __queue_;
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

inline auto tag_invoke(
    std::execution::get_forward_progress_guarantee_t,
    const static_thread_pool::scheduler&) noexcept
    -> std::execution::forward_progress_guarantee {
    return std::execution::forward_progress_guarantee::parallel;
}

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

    try {
        if (!__pool->__submit([this]() noexcept {
            std::execution::set_value(std::move(__rcvr));
        })) {
            std::execution::set_stopped(std::move(__rcvr));
        }
    } catch (...) {
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
