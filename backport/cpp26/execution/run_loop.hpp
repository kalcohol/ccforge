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

#include "concepts.hpp"
#include "env.hpp"

#include <condition_variable>
#include <cstddef>
#include <mutex>

namespace std::execution {

class run_loop {
public:
    run_loop() noexcept = default;
    run_loop(const run_loop&) = delete;
    run_loop& operator=(const run_loop&) = delete;
    run_loop(run_loop&&) = delete;
    run_loop& operator=(run_loop&&) = delete;

    ~run_loop() {
        if (__task_count_ > 0) {
            std::terminate();
        }
    }

    class scheduler;

    [[nodiscard]] scheduler get_scheduler() noexcept;

    void run();

    void finish() noexcept {
        std::lock_guard lk{__mtx_};
        __finishing_ = true;
        __cv_.notify_all();
    }

    struct __task_base {
        __task_base* __next = nullptr;
        void (*__execute)(__task_base*) noexcept = nullptr;
    };

    void __push(__task_base* t) noexcept {
        std::lock_guard lk{__mtx_};
        t->__next = nullptr;
        if (__tail_) {
            __tail_->__next = t;
        } else {
            __head_ = t;
        }
        __tail_ = t;
        ++__task_count_;
        __cv_.notify_one();
    }

    __task_base* __pop() noexcept {
        std::unique_lock lk{__mtx_};
        __cv_.wait(lk, [this] { return __head_ != nullptr || __finishing_; });
        if (__head_ == nullptr) {
            return nullptr;
        }
        auto* t = __head_;
        __head_ = t->__next;
        if (__head_ == nullptr) {
            __tail_ = nullptr;
        }
        return t;
    }

    std::mutex __mtx_{};
    std::condition_variable __cv_{};
    __task_base* __head_ = nullptr;
    __task_base* __tail_ = nullptr;
    bool __finishing_ = false;
    std::size_t __task_count_ = 0;
};

inline void run_loop::run() {
    while (true) {
        __task_base* t = __pop();
        if (t == nullptr) {
            break;
        }
        t->__execute(t);
        {
            std::lock_guard lk{__mtx_};
            --__task_count_;
        }
    }
}

namespace __forge_run_loop {

struct __env {
    run_loop::scheduler const* __sched;
    friend auto tag_invoke(get_scheduler_t, const __env& self) noexcept
        -> run_loop::scheduler;
};

template<class R>
struct __op : run_loop::__task_base, __forge_detail::__immovable {
    using operation_state_concept = operation_state_t;

    run_loop* __loop;
    R __rcvr;

    __op(run_loop* loop, R rcvr) : __loop(loop), __rcvr(std::move(rcvr)) {
        this->__execute = [](run_loop::__task_base* t) noexcept {
            auto* self = static_cast<__op*>(t);
            set_value(std::move(self->__rcvr));
        };
    }

    friend void tag_invoke(start_t, __op& self) noexcept {
        self.__loop->__push(&self);
    }
};

struct __sender {
    using sender_concept = sender_t;
    run_loop* __loop;
    run_loop::scheduler const* __sched;

    friend auto tag_invoke(get_completion_signatures_t, const __sender&, auto) noexcept
        -> completion_signatures<set_value_t()> {
        return {};
    }

    template<receiver R>
    friend auto tag_invoke(connect_t, __sender self, R rcvr) -> __op<R> {
        return __op<R>{self.__loop, std::move(rcvr)};
    }

    friend auto tag_invoke(get_env_t, const __sender& self) noexcept -> __env {
        return __env{self.__sched};
    }
};

} // namespace __forge_run_loop

class run_loop::scheduler {
public:
    using scheduler_concept = scheduler_t;

    bool operator==(const scheduler&) const noexcept = default;

    friend auto tag_invoke(schedule_t, const scheduler& self) noexcept
        -> __forge_run_loop::__sender {
        return __forge_run_loop::__sender{self.__loop, &self};
    }

private:
    friend class run_loop;
    friend auto tag_invoke(get_scheduler_t,
                           const __forge_run_loop::__env& env) noexcept
        -> scheduler;
    explicit scheduler(run_loop* loop) noexcept : __loop(loop) {}
    run_loop* __loop;
};

inline run_loop::scheduler run_loop::get_scheduler() noexcept {
    return scheduler{this};
}

namespace __forge_run_loop {
inline auto tag_invoke(get_scheduler_t, const __env& self) noexcept
    -> run_loop::scheduler {
    return *self.__sched;
}
} // namespace __forge_run_loop

} // namespace std::execution
