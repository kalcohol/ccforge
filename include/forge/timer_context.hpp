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
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <thread>
#include <vector>

namespace forge {

class timer_context;

struct timer_context_options {
    std::pmr::memory_resource* memory = default_memory_resource();
};

namespace __timer_detail {

struct __state;

struct __item {
    std::chrono::steady_clock::time_point deadline;
    std::any_stop_token stop_token;
    std::function<void()> complete_value;
    std::function<void()> complete_stopped;
};

template<class R>
struct __op_data {
    explicit __op_data(R rcvr)
        : rcvr_(std::move(rcvr))
    {}

    void complete_value() noexcept {
        if (done_.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        std::execution::set_value(std::move(rcvr_));
    }

    void complete_stopped() noexcept {
        if (done_.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        std::execution::set_stopped(std::move(rcvr_));
    }

    R rcvr_;
    std::atomic<bool> done_{false};
};

template<class R>
struct __op {
    using operation_state_concept = std::execution::operation_state_t;

    __op(__op&&) = delete;
    __op& operator=(__op&&) = delete;
    __op(const __op&) = delete;
    __op& operator=(const __op&) = delete;

    __op(std::shared_ptr<__state> state, std::chrono::steady_clock::time_point deadline, R rcvr)
        : state_(std::move(state))
        , deadline_(deadline)
        , data_(make_data(state_, std::move(rcvr)))
    {}

    void start() & noexcept;

    static auto make_data(std::shared_ptr<__state> state, R rcvr)
        -> std::shared_ptr<__op_data<R>>;

    std::shared_ptr<__state> state_;
    std::chrono::steady_clock::time_point deadline_;
    std::shared_ptr<__op_data<R>> data_;
    std::shared_ptr<__item> item_;
};

struct __sender {
    using sender_concept = std::execution::sender_t;

    std::shared_ptr<__state> state;
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
        return __op<R>{std::move(state), deadline, std::move(rcvr)};
    }

    template<std::execution::receiver R>
    auto connect(R rcvr) const& -> __op<R> {
        return __op<R>{state, deadline, std::move(rcvr)};
    }
};

struct __state {
    explicit __state(std::pmr::memory_resource* memory_resource)
        : memory(normalize_memory_resource(memory_resource))
        , items(memory)
    {}

    [[nodiscard]] auto make_item() -> std::shared_ptr<__item> {
        return std::allocate_shared<__item>(
            std::pmr::polymorphic_allocator<__item>{memory});
    }

    bool enqueue(std::shared_ptr<__item> item) {
        std::lock_guard lk{mtx};
        if (stop) {
            return false;
        }
        items.push_back(std::move(item));
        ++pending;
        cv.notify_all();
        return true;
    }

    void shutdown() noexcept {
        {
            std::lock_guard lk{mtx};
            stop = true;
        }
        cv.notify_all();
    }

    void wait() noexcept {
        std::unique_lock lk{mtx};
        if (worker_id == std::this_thread::get_id()) {
            return;
        }
        cv_wait.wait(lk, [this] { return pending == 0; });
    }

    void run() noexcept {
        {
            std::lock_guard lk{mtx};
            worker_id = std::this_thread::get_id();
        }

        while (true) {
            std::shared_ptr<__item> ready;
            bool complete_stopped = false;

            {
                std::unique_lock lk{mtx};
                while (true) {
                    if (stop) {
                        if (items.empty()) {
                            if (pending == 0) {
                                cv_wait.notify_all();
                            }
                            return;
                        }
                        ready = std::move(items.back());
                        items.pop_back();
                        complete_stopped = true;
                        break;
                    }

                    if (items.empty()) {
                        if (pending == 0) {
                            cv_wait.notify_all();
                        }
                        cv.wait(lk);
                        continue;
                    }

                    const auto now = std::chrono::steady_clock::now();
                    auto stopped_it = std::find_if(items.begin(), items.end(),
                        [](const std::shared_ptr<__item>& item) {
                            return item->stop_token.stop_requested();
                        });
                    if (stopped_it != items.end()) {
                        ready = std::move(*stopped_it);
                        items.erase(stopped_it);
                        complete_stopped = true;
                        break;
                    }

                    auto next_it = std::min_element(items.begin(), items.end(),
                        [](const std::shared_ptr<__item>& lhs,
                           const std::shared_ptr<__item>& rhs) {
                            return lhs->deadline < rhs->deadline;
                        });

                    if ((*next_it)->deadline <= now) {
                        ready = std::move(*next_it);
                        items.erase(next_it);
                        break;
                    }

                    auto wake = (*next_it)->deadline;
                    if (has_stoppable_item()) {
                        const auto poll_wake = now + poll_interval;
                        if (poll_wake < wake) {
                            wake = poll_wake;
                        }
                    }
                    cv.wait_until(lk, wake);
                }
            }

            if (ready) {
                if (complete_stopped) {
                    ready->complete_stopped();
                } else {
                    ready->complete_value();
                }

                {
                    std::lock_guard lk{mtx};
                    --pending;
                    if (pending == 0) {
                        cv_wait.notify_all();
                    }
                }
            }
        }
    }

    bool has_stoppable_item() const noexcept {
        return std::any_of(items.begin(), items.end(),
            [](const std::shared_ptr<__item>& item) {
                return item->stop_token.stop_possible();
            });
    }

    static constexpr auto poll_interval = std::chrono::milliseconds(1);

    std::mutex mtx;
    std::condition_variable cv;
    std::condition_variable cv_wait;
    std::pmr::memory_resource* memory;
    std::pmr::vector<std::shared_ptr<__item>> items;
    bool stop = false;
    std::size_t pending = 0;
    std::thread::id worker_id{};
};

} // namespace __timer_detail

class timer_context {
public:
    timer_context()
        : timer_context(timer_context_options{})
    {}

    explicit timer_context(timer_context_options options)
        : state_(std::allocate_shared<__timer_detail::__state>(
              std::pmr::polymorphic_allocator<__timer_detail::__state>{
                  normalize_memory_resource(options.memory)},
              normalize_memory_resource(options.memory)))
        , thread_([state = state_] { state->run(); }) {}

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
        state_->shutdown();
    }

    void wait() noexcept {
        state_->wait();
    }

private:
    template<class R>
    friend struct __timer_detail::__op;

    [[nodiscard]] auto schedule_at_steady(
        std::chrono::steady_clock::time_point deadline) noexcept
            -> __timer_detail::__sender {
        return __timer_detail::__sender{state_, deadline};
    }

    std::shared_ptr<__timer_detail::__state> state_;
    std::thread thread_;
};

namespace __timer_detail {

template<class R>
inline auto __op<R>::make_data(std::shared_ptr<__state> state, R rcvr)
    -> std::shared_ptr<__op_data<R>> {
    auto* memory = state ? state->memory : default_memory_resource();
    return std::allocate_shared<__op_data<R>>(
        std::pmr::polymorphic_allocator<__op_data<R>>{memory},
        std::move(rcvr));
}

template<class R>
inline void __op<R>::start() & noexcept {
    auto data = data_;
    try {
        item_ = state_
            ? state_->make_item()
            : std::allocate_shared<__item>(
                  std::pmr::polymorphic_allocator<__item>{
                      default_memory_resource()});
        item_->deadline = deadline_;
        item_->complete_value = [data] { data->complete_value(); };
        item_->complete_stopped = [data] { data->complete_stopped(); };

        auto env = std::execution::get_env(data->rcvr_);
        auto token = std::execution::get_stop_token(env);
        if (token.stop_requested()) {
            data->complete_stopped();
            return;
        }
        if (token.stop_possible()) {
            item_->stop_token = std::any_stop_token{std::move(token)};
        }

        if (!state_ || !state_->enqueue(item_)) {
            data->complete_stopped();
        }
    } catch (...) {
        data->complete_stopped();
    }
}

} // namespace __timer_detail

} // namespace forge
