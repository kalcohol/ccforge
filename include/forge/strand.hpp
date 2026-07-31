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
#include <exception>
#include <list>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <type_traits>
#include <utility>

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

    std::shared_ptr<__record_base> deferred_stopped_next_;
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
struct __current_state_guard;
inline thread_local __current_state_guard* __current_guard = nullptr;

struct __current_state_guard {
    explicit __current_state_guard(__state* state) noexcept
        : state(state)
        , previous(__current_guard) {
        __current_guard = this;
    }

    ~__current_state_guard() {
        __current_guard = previous;
    }

    __state* state;
    __current_state_guard* previous;
};

struct __runner_base {
    explicit __runner_base(std::pmr::memory_resource* memory) noexcept
        : memory_(normalize_memory_resource(memory))
    {}

    __runner_base(const __runner_base&) = delete;
    __runner_base& operator=(const __runner_base&) = delete;

    void add_ref() noexcept {
        refs_.fetch_add(1, std::memory_order_relaxed);
    }

    void release() noexcept {
        if (refs_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            destroy_self();
        }
    }

    void start() noexcept {
        add_ref();
        start_impl();
        release();
    }

    virtual void start_impl() noexcept = 0;
    virtual void destroy_self() noexcept = 0;

protected:
    virtual ~__runner_base() = default;

    [[nodiscard]] auto memory_resource() const noexcept -> std::pmr::memory_resource* {
        return memory_;
    }

private:
    std::pmr::memory_resource* memory_;
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

    __runner(
        std::pmr::memory_resource* memory,
        std::shared_ptr<__state> state,
        Sender sender)
        : __runner_base(memory)
        , op_(std::execution::connect(
              std::move(sender),
              __runner_recv{std::move(state), this}))
    {}

    void start_impl() noexcept override {
        std::execution::start(op_);
    }

    void destroy_self() noexcept override {
        auto* memory = memory_resource();
        std::pmr::polymorphic_allocator<__runner> alloc{memory};
        std::destroy_at(this);
        alloc.deallocate(this, 1);
    }

    op_t op_;
};

struct __state : std::enable_shared_from_this<__state> {
    using queue_t = std::pmr::list<std::shared_ptr<__record_base>>;

    __state(any_scheduler scheduler, strand_options options)
        : scheduler_(std::move(scheduler))
        , memory_(normalize_memory_resource(options.memory))
        , queue_(memory_)
    {}

    void enqueue(std::shared_ptr<__record_base> record) noexcept {
        auto keepalive = shared_from_this();
        queue_t stopped{memory_};
        std::shared_ptr<__record_base> deferred_stopped;
        bool launch = false;
        bool drain = false;
        {
            std::lock_guard lk{mtx_};
            if (closed_) {
                append_deferred_stopped_locked(std::move(record));
                if (!active_) {
                    active_ = true;
                    running_ = true;
                    stopped.splice(stopped.end(), queue_);
                    deferred_stopped = take_deferred_stopped_locked();
                    drain = true;
                }
            } else {
                try {
                    queue_.push_back(record);
                    if (!running_) {
                        running_ = true;
                        launch = true;
                    }
                } catch (...) {
                    closed_ = true;
                    append_deferred_stopped_locked(std::move(record));
                    if (!active_) {
                        active_ = true;
                        running_ = true;
                        stopped.splice(stopped.end(), queue_);
                        deferred_stopped = take_deferred_stopped_locked();
                        drain = true;
                    }
                }
            }
        }

        if (drain) {
            complete_stopped_batch(stopped, std::move(deferred_stopped));
        }
        if (launch) {
            launch_runner();
        }
    }

    void shutdown() noexcept {
        auto keepalive = shared_from_this();
        queue_t stopped{memory_};
        std::shared_ptr<__record_base> deferred_stopped;
        bool drain = false;
        bool notify = false;
        {
            std::lock_guard lk{mtx_};
            closed_ = true;
            if (!active_) {
                if (queue_.empty() && !deferred_stopped_head_) {
                    running_ = false;
                    notify = true;
                } else {
                    stopped.splice(stopped.end(), queue_);
                    deferred_stopped = take_deferred_stopped_locked();
                    active_ = true;
                    running_ = true;
                    drain = true;
                }
            }
        }

        if (drain) {
            complete_stopped_batch(stopped, std::move(deferred_stopped));
        } else if (notify) {
            cv_.notify_all();
        }
    }

    void run_one() noexcept {
        std::shared_ptr<__record_base> record;
        queue_t stopped{memory_};
        std::shared_ptr<__record_base> deferred_stopped;
        bool drain = false;
        {
            std::lock_guard lk{mtx_};
            if (active_) {
                return;
            }
            if (queue_.empty()) {
                if (closed_ && deferred_stopped_head_) {
                    active_ = true;
                    running_ = true;
                    deferred_stopped = take_deferred_stopped_locked();
                    drain = true;
                } else {
                    running_ = false;
                    cv_.notify_all();
                    return;
                }
            } else {
                active_ = true;
                if (closed_) {
                    stopped.splice(stopped.end(), queue_);
                    deferred_stopped = take_deferred_stopped_locked();
                    drain = true;
                } else {
                    record = std::move(queue_.front());
                    queue_.pop_front();
                }
            }
        }

        if (drain) {
            complete_stopped_batch(stopped, std::move(deferred_stopped));
            return;
        }

        {
            __current_state_guard guard{this};
            record->complete_value();
        }

        bool launch = false;
        bool drain_after_value = false;
        {
            std::lock_guard lk{mtx_};
            if (closed_ &&
                (!queue_.empty() || deferred_stopped_head_)) {
                stopped.splice(stopped.end(), queue_);
                deferred_stopped = take_deferred_stopped_locked();
                drain_after_value = true;
            } else {
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
        }

        if (drain_after_value) {
            complete_stopped_batch(stopped, std::move(deferred_stopped));
        } else if (launch) {
            launch_runner();
        }
    }

    void stop_all() noexcept {
        shutdown();
    }

    void wait() noexcept {
        for (auto* guard = __current_guard;
             guard != nullptr;
             guard = guard->previous) {
            if (guard->state == this) {
                return;
            }
        }
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
    void append_deferred_stopped_locked(
        std::shared_ptr<__record_base> record) noexcept {
        record->deferred_stopped_next_.reset();
        auto* tail = record.get();
        if (deferred_stopped_tail_) {
            deferred_stopped_tail_->deferred_stopped_next_ = std::move(record);
        } else {
            deferred_stopped_head_ = std::move(record);
        }
        deferred_stopped_tail_ = tail;
    }

    auto take_deferred_stopped_locked() noexcept
        -> std::shared_ptr<__record_base> {
        deferred_stopped_tail_ = nullptr;
        return std::exchange(deferred_stopped_head_, {});
    }

    void complete_stopped_batch(
        queue_t& stopped,
        std::shared_ptr<__record_base> deferred_stopped = {}) noexcept {
        __current_state_guard guard{this};
        for (;;) {
            for (auto& record : stopped) {
                record->complete_stopped();
            }
            stopped.clear();

            while (deferred_stopped) {
                auto record = std::move(deferred_stopped);
                deferred_stopped =
                    std::move(record->deferred_stopped_next_);
                record->complete_stopped();
            }

            bool complete = false;
            {
                std::lock_guard lk{mtx_};
                stopped.splice(stopped.end(), queue_);
                deferred_stopped = take_deferred_stopped_locked();
                if (stopped.empty() && !deferred_stopped) {
                    active_ = false;
                    running_ = false;
                    complete = true;
                }
            }
            if (complete) {
                break;
            }
        }
        cv_.notify_all();
    }

    void launch_runner() noexcept {
        try {
            auto sender = std::execution::schedule(scheduler_);
            using sender_t = decltype(sender);
            using runner_t = __runner<sender_t>;
            std::pmr::polymorphic_allocator<runner_t> alloc{memory_};
            auto* runner = alloc.allocate(1);
            try {
                std::construct_at(runner,
                    memory_,
                    this->shared_from_this(),
                    std::move(sender));
            } catch (...) {
                alloc.deallocate(runner, 1);
                throw;
            }
            runner->start();
        } catch (...) {
            stop_all();
        }
    }

    any_scheduler scheduler_;
    std::pmr::memory_resource* memory_;
    mutable std::mutex mtx_;
    std::condition_variable cv_;
    queue_t queue_;
    std::shared_ptr<__record_base> deferred_stopped_head_;
    __record_base* deferred_stopped_tail_ = nullptr;
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
