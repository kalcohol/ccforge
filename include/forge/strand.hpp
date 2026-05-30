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

#include "any_scheduler.hpp"
#include "resource_policy.hpp"

#include <execution>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <exception>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <type_traits>
#include <utility>
#include <vector>

namespace forge {

class strand;

struct strand_options {
    std::pmr::memory_resource* memory = default_memory_resource();
};

namespace __strand_detail {

using __completion_signatures = std::execution::completion_signatures<
    std::execution::set_value_t(),
    std::execution::set_stopped_t()>;

struct __record_base {
    virtual ~__record_base() = default;
    virtual void complete_value() noexcept = 0;
    virtual void complete_stopped() noexcept = 0;
};

template<class R>
struct __record final : __record_base {
    explicit __record(R rcvr) : rcvr_(std::move(rcvr)) {}

    void complete_value() noexcept override {
        if (done_.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        std::execution::set_value(std::move(rcvr_));
    }

    void complete_stopped() noexcept override {
        if (done_.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        std::execution::set_stopped(std::move(rcvr_));
    }

    R rcvr_;
    std::atomic<bool> done_{false};
};

struct __state;

struct __runner_base {
    __runner_base() = default;
    __runner_base(const __runner_base&) = delete;
    __runner_base& operator=(const __runner_base&) = delete;

    void add_ref() noexcept {
        refs_.fetch_add(1, std::memory_order_relaxed);
    }

    void release() noexcept {
        if (refs_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            delete this;
        }
    }

    void start() noexcept {
        add_ref();
        start_impl();
        release();
    }

    virtual void start_impl() noexcept = 0;

protected:
    virtual ~__runner_base() = default;

private:
    std::atomic<unsigned> refs_{1};
};

struct __runner_recv {
    using receiver_concept = std::execution::receiver_t;

    std::shared_ptr<__state> state;
    __runner_base* node;

    void set_value() && noexcept;
    void set_stopped() && noexcept;
    void set_error(std::exception_ptr) && noexcept;
    auto get_env() const noexcept -> std::execution::empty_env { return {}; }
};

template<class Sender>
struct __runner final : __runner_base {
    using op_t = std::execution::connect_result_t<Sender, __runner_recv>;

    __runner(std::shared_ptr<__state> state, Sender sender)
        : op_(std::execution::connect(
              std::move(sender),
              __runner_recv{std::move(state), this}))
    {}

    void start_impl() noexcept override {
        std::execution::start(op_);
    }

    op_t op_;
};

struct __state : std::enable_shared_from_this<__state> {
    __state(any_scheduler scheduler, strand_options options)
        : scheduler_(std::move(scheduler))
        , memory_(normalize_memory_resource(options.memory))
        , queue_(memory_)
    {}

    void enqueue(std::shared_ptr<__record_base> record) noexcept {
        std::shared_ptr<__record_base> stopped;
        bool launch = false;
        {
            std::lock_guard lk{mtx_};
            if (closed_) {
                stopped = std::move(record);
            } else {
                queue_.push_back(std::move(record));
                if (!running_) {
                    running_ = true;
                    launch = true;
                }
            }
        }

        if (stopped) {
            stopped->complete_stopped();
        }
        if (launch) {
            launch_runner();
        }
    }

    void shutdown() noexcept {
        std::pmr::vector<std::shared_ptr<__record_base>> stopped{memory_};
        bool notify = false;
        {
            std::lock_guard lk{mtx_};
            closed_ = true;
            while (!queue_.empty()) {
                stopped.push_back(std::move(queue_.front()));
                queue_.pop_front();
            }
            if (!active_ && queue_.empty()) {
                running_ = false;
                notify = true;
            }
        }

        if (notify) {
            cv_.notify_all();
        }

        for (auto& record : stopped) {
            record->complete_stopped();
        }
    }

    void run_one() noexcept {
        std::shared_ptr<__record_base> record;
        {
            std::lock_guard lk{mtx_};
            if (queue_.empty()) {
                running_ = false;
                cv_.notify_all();
                return;
            }
            record = std::move(queue_.front());
            queue_.pop_front();
            active_ = true;
        }

        record->complete_value();

        bool launch = false;
        {
            std::lock_guard lk{mtx_};
            active_ = false;
            if (queue_.empty()) {
                running_ = false;
            } else {
                launch = true;
            }
            if (!running_) {
                cv_.notify_all();
            }
        }

        if (launch) {
            launch_runner();
        }
    }

    void stop_all() noexcept {
        std::pmr::vector<std::shared_ptr<__record_base>> stopped{memory_};
        {
            std::lock_guard lk{mtx_};
            closed_ = true;
            running_ = false;
            while (!queue_.empty()) {
                stopped.push_back(std::move(queue_.front()));
                queue_.pop_front();
            }
            cv_.notify_all();
        }

        for (auto& record : stopped) {
            record->complete_stopped();
        }
    }

    void wait() noexcept {
        std::unique_lock lk{mtx_};
        cv_.wait(lk, [this] {
            return queue_.empty() && !running_;
        });
    }

    [[nodiscard]] bool closed() const noexcept {
        std::lock_guard lk{mtx_};
        return closed_;
    }

    [[nodiscard]] auto memory_resource() const noexcept
        -> std::pmr::memory_resource* {
        return memory_;
    }

private:
    void launch_runner() noexcept {
        try {
            auto sender = std::execution::schedule(scheduler_);
            using sender_t = decltype(sender);
            auto* runner = new __runner<sender_t>{
                this->shared_from_this(),
                std::move(sender)};
            runner->start();
        } catch (...) {
            stop_all();
        }
    }

    any_scheduler scheduler_;
    std::pmr::memory_resource* memory_;
    mutable std::mutex mtx_;
    std::condition_variable cv_;
    std::pmr::deque<std::shared_ptr<__record_base>> queue_;
    bool running_ = false;
    bool active_ = false;
    bool closed_ = false;
};

inline void __runner_recv::set_value() && noexcept {
    state->run_one();
    node->release();
}

inline void __runner_recv::set_stopped() && noexcept {
    state->stop_all();
    node->release();
}

inline void __runner_recv::set_error(std::exception_ptr) && noexcept {
    state->stop_all();
    node->release();
}

struct __env;
struct __sender;
template<class R>
struct __op;

struct __scheduler {
    using scheduler_concept = std::execution::scheduler_t;

    std::shared_ptr<__state> state;

    auto schedule() const noexcept -> __sender;

    friend bool operator==(const __scheduler&, const __scheduler&) noexcept = default;
};

struct __env {
    std::shared_ptr<__state> state;
};

struct __sender {
    using sender_concept = std::execution::sender_t;

    std::shared_ptr<__state> state;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept -> __completion_signatures {
        return {};
    }

    auto get_env() const noexcept -> __env {
        return __env{state};
    }

    template<class R>
        requires std::execution::receiver_of<R, __completion_signatures>
    auto connect(R rcvr) && -> __op<R>;

    template<class R>
        requires std::execution::receiver_of<R, __completion_signatures>
    auto connect(R rcvr) const& -> __op<R>;
};

template<class R>
struct __op {
    using operation_state_concept = std::execution::operation_state_t;
    using record_t = __record<R>;

    __op(std::shared_ptr<__state> state, R rcvr)
        : state_(std::move(state))
        , record_(std::allocate_shared<record_t>(
              std::pmr::polymorphic_allocator<record_t>{
                  state_->memory_resource()},
              std::move(rcvr)))
    {}

    __op(__op&&) = delete;
    __op& operator=(__op&&) = delete;
    __op(const __op&) = delete;
    __op& operator=(const __op&) = delete;

    void start() & noexcept {
        if (stop_requested(record_->rcvr_)) {
            record_->complete_stopped();
            return;
        }
        state_->enqueue(record_);
    }

    static bool stop_requested(const R& rcvr) noexcept {
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

    std::shared_ptr<__state> state_;
    std::shared_ptr<record_t> record_;
};

template<class R>
    requires std::execution::receiver_of<R, __completion_signatures>
auto __sender::connect(R rcvr) && -> __op<R> {
    return __op<R>{std::move(state), std::move(rcvr)};
}

template<class R>
    requires std::execution::receiver_of<R, __completion_signatures>
auto __sender::connect(R rcvr) const& -> __op<R> {
    return __op<R>{state, std::move(rcvr)};
}

inline auto tag_invoke(
    std::execution::get_completion_scheduler_t<std::execution::set_value_t>,
    const __env& env) noexcept -> __scheduler {
    return __scheduler{env.state};
}

inline auto __scheduler::schedule() const noexcept -> __sender {
    return __sender{state};
}

} // namespace __strand_detail

class strand {
public:
    using scheduler = __strand_detail::__scheduler;

    explicit strand(any_scheduler scheduler)
        : strand(std::move(scheduler), strand_options{})
    {}

    strand(any_scheduler scheduler, strand_options options)
        : state_(__make_state(std::move(scheduler), options))
    {}

    template<class Scheduler>
        requires (!std::is_same_v<std::remove_cvref_t<Scheduler>, strand>)
              && (!std::is_same_v<std::remove_cvref_t<Scheduler>, any_scheduler>)
              && std::execution::scheduler<std::remove_cvref_t<Scheduler>>
    explicit strand(Scheduler&& scheduler)
        : strand(any_scheduler{static_cast<Scheduler&&>(scheduler)})
    {}

    template<class Scheduler>
        requires (!std::is_same_v<std::remove_cvref_t<Scheduler>, strand>)
              && (!std::is_same_v<std::remove_cvref_t<Scheduler>, any_scheduler>)
              && std::execution::scheduler<std::remove_cvref_t<Scheduler>>
    strand(Scheduler&& scheduler, strand_options options)
        : strand(any_scheduler{static_cast<Scheduler&&>(scheduler)}, options)
    {}

    strand(const strand&) = delete;
    strand& operator=(const strand&) = delete;
    strand(strand&&) = delete;
    strand& operator=(strand&&) = delete;

    ~strand() noexcept {
        shutdown();
        wait();
    }

    [[nodiscard]] scheduler get_scheduler() const noexcept {
        return scheduler{state_};
    }

    void shutdown() noexcept {
        state_->shutdown();
    }

    void wait() noexcept {
        state_->wait();
    }

    [[nodiscard]] bool closed() const noexcept {
        return state_->closed();
    }

private:
    static auto __make_state(any_scheduler scheduler, strand_options options)
        -> std::shared_ptr<__strand_detail::__state> {
        options.memory = normalize_memory_resource(options.memory);
        return std::allocate_shared<__strand_detail::__state>(
            std::pmr::polymorphic_allocator<__strand_detail::__state>{
                options.memory},
            std::move(scheduler),
            options);
    }

    std::shared_ptr<__strand_detail::__state> state_;
};

} // namespace forge
