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
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace forge {

class timer_context;

namespace __timer_detail {

struct __item {
    std::chrono::steady_clock::time_point deadline;
    std::function<void()> complete_value;
    std::function<void()> complete_stopped;
    std::atomic<bool> done{false};
};

struct __item_later {
    bool operator()(
        const std::shared_ptr<__item>& lhs,
        const std::shared_ptr<__item>& rhs) const noexcept {
        return lhs->deadline > rhs->deadline;
    }
};

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
struct __op {
    using operation_state_concept = std::execution::operation_state_t;

    __op(__op&&) = delete;
    __op& operator=(__op&&) = delete;
    __op(const __op&) = delete;
    __op& operator=(const __op&) = delete;

    __op(timer_context* context, std::chrono::steady_clock::time_point deadline, R rcvr)
        : context_(context), deadline_(deadline), rcvr_(std::move(rcvr)) {}

    void start() & noexcept;

    void complete_value() noexcept {
        if (item_->done.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        std::execution::set_value(std::move(rcvr_));
    }

    void complete_stopped() noexcept {
        if (item_->done.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        std::execution::set_stopped(std::move(rcvr_));
    }

    timer_context* context_;
    std::chrono::steady_clock::time_point deadline_;
    R rcvr_;
    std::shared_ptr<__item> item_;
};

struct __sender {
    using sender_concept = std::execution::sender_t;

    timer_context* context;
    std::chrono::steady_clock::time_point deadline;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<
            std::execution::set_value_t(),
            std::execution::set_stopped_t()> {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }

    template<std::execution::receiver R>
    auto connect(R rcvr) && -> __op<R> {
        return __op<R>{context, deadline, std::move(rcvr)};
    }

    template<std::execution::receiver R>
    auto connect(R rcvr) const& -> __op<R> {
        return __op<R>{context, deadline, std::move(rcvr)};
    }
};

} // namespace __timer_detail

class timer_context {
public:
    timer_context()
        : thread_([this] { run(); }) {}

    ~timer_context() noexcept {
        shutdown();
        if (thread_.joinable()) {
            if (thread_.get_id() == std::this_thread::get_id()) {
                thread_.detach();
            } else {
                thread_.join();
            }
        }
    }

    timer_context(const timer_context&) = delete;
    timer_context& operator=(const timer_context&) = delete;
    timer_context(timer_context&&) = delete;
    timer_context& operator=(timer_context&&) = delete;

    template<class Rep, class Period>
    [[nodiscard]] auto schedule_after(std::chrono::duration<Rep, Period> delay)
        -> __timer_detail::__sender {
        auto steady_delay =
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(delay);
        if (steady_delay < std::chrono::steady_clock::duration::zero()) {
            steady_delay = std::chrono::steady_clock::duration::zero();
        }
        return schedule_at_steady(std::chrono::steady_clock::now() + steady_delay);
    }

    template<class Clock, class Duration>
    [[nodiscard]] auto schedule_at(std::chrono::time_point<Clock, Duration> time)
        -> __timer_detail::__sender {
        auto now = Clock::now();
        auto steady_now = std::chrono::steady_clock::now();
        if (time <= now) {
            return schedule_at_steady(steady_now);
        }
        auto delay =
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(time - now);
        return schedule_at_steady(steady_now + delay);
    }

    void shutdown() noexcept {
        {
            std::lock_guard lk{mtx_};
            stop_ = true;
        }
        cv_.notify_all();
    }

private:
    template<class R>
    friend struct __timer_detail::__op;

    [[nodiscard]] auto schedule_at_steady(
        std::chrono::steady_clock::time_point deadline) noexcept
            -> __timer_detail::__sender {
        return __timer_detail::__sender{this, deadline};
    }

    bool enqueue(std::shared_ptr<__timer_detail::__item> item) {
        std::lock_guard lk{mtx_};
        if (stop_) {
            return false;
        }
        queue_.push(std::move(item));
        cv_.notify_all();
        return true;
    }

    void run() noexcept {
        while (true) {
            std::vector<std::shared_ptr<__timer_detail::__item>> stopped;
            std::shared_ptr<__timer_detail::__item> ready;
            bool shutting_down = false;

            {
                std::unique_lock lk{mtx_};
                while (true) {
                    if (stop_) {
                        shutting_down = true;
                        while (!queue_.empty()) {
                            stopped.push_back(queue_.top());
                            queue_.pop();
                        }
                        break;
                    }

                    if (queue_.empty()) {
                        cv_.wait(lk);
                        continue;
                    }

                    auto deadline = queue_.top()->deadline;
                    if (deadline > std::chrono::steady_clock::now()) {
                        cv_.wait_until(lk, deadline);
                        continue;
                    }

                    ready = queue_.top();
                    queue_.pop();
                    break;
                }
            }

            if (!stopped.empty()) {
                for (auto& item : stopped) {
                    item->complete_stopped();
                }
            }

            if (shutting_down) {
                return;
            }

            if (ready) {
                ready->complete_value();
            }
        }
    }

    std::mutex mtx_;
    std::condition_variable cv_;
    std::priority_queue<
        std::shared_ptr<__timer_detail::__item>,
        std::vector<std::shared_ptr<__timer_detail::__item>>,
        __timer_detail::__item_later> queue_;
    std::thread thread_;
    bool stop_ = false;
};

namespace __timer_detail {

template<class R>
inline void __op<R>::start() & noexcept {
    item_ = std::make_shared<__item>();
    item_->deadline = deadline_;
    item_->complete_value = [this] { complete_value(); };
    item_->complete_stopped = [this] { complete_stopped(); };

    if (__stop_requested(rcvr_)) {
        complete_stopped();
        return;
    }

    if (!context_->enqueue(item_)) {
        complete_stopped();
    }
}

} // namespace __timer_detail

} // namespace forge
