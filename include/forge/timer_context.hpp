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

#include "any_stop_token.hpp"
#include "resource_policy.hpp"

#include <execution>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <type_traits>
#include <optional>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>

namespace forge {

class timer_context;

struct timer_context_options {
    std::pmr::memory_resource* memory = default_memory_resource();
};

namespace __timer_detail {

struct __state;
struct __item;

template<class ToDuration, class Rep, class Period>
[[nodiscard]] constexpr auto __saturating_nonnegative_duration_cast(
    std::chrono::duration<Rep, Period> value) noexcept -> ToDuration {
    using wide_duration =
        std::chrono::duration<long double, typename ToDuration::period>;

    const auto ticks = std::chrono::duration_cast<wide_duration>(value).count();
    if (!(ticks > 0.0L)) {
        return ToDuration::zero();
    }

    const auto max_ticks = static_cast<long double>(ToDuration::max().count());
    if (ticks >= max_ticks) {
        return ToDuration::max();
    }
    return ToDuration{static_cast<typename ToDuration::rep>(ticks)};
}

template<class ToDuration, class Clock, class TargetDuration>
[[nodiscard]] constexpr auto __saturating_time_difference(
    std::chrono::time_point<Clock, TargetDuration> target,
    std::chrono::time_point<Clock> now) noexcept -> ToDuration {
    using wide_duration =
        std::chrono::duration<long double, typename ToDuration::period>;

    const auto target_ticks = std::chrono::duration_cast<wide_duration>(
        target.time_since_epoch()).count();
    const auto now_ticks = std::chrono::duration_cast<wide_duration>(
        now.time_since_epoch()).count();
    return __saturating_nonnegative_duration_cast<ToDuration>(
        wide_duration{target_ticks - now_ticks});
}

template<class Clock>
[[nodiscard]] constexpr auto __saturating_time_add(
    std::chrono::time_point<Clock> base,
    typename Clock::duration delay) noexcept -> std::chrono::time_point<Clock> {
    if (delay > Clock::duration::zero()
        && base.time_since_epoch() > Clock::duration::max() - delay) {
        return std::chrono::time_point<Clock>::max();
    }
    return base + delay;
}

// Absolute deadlines pass through unchanged; relative delays anchor to the
// moment the operation starts, so a lazily started sender still waits its
// full delay instead of a delay measured from composition time.
struct __when {
    bool relative = false;
    std::chrono::steady_clock::duration delay{};
    std::chrono::steady_clock::time_point deadline{};

    [[nodiscard]] static auto at(
        std::chrono::steady_clock::time_point deadline) noexcept -> __when {
        __when when;
        when.deadline = deadline;
        return when;
    }

    [[nodiscard]] static auto after(
        std::chrono::steady_clock::duration delay) noexcept -> __when {
        __when when;
        when.relative = true;
        when.delay = delay;
        return when;
    }

    [[nodiscard]] auto resolve_at_start() const noexcept
        -> std::chrono::steady_clock::time_point {
        if (!relative) {
            return deadline;
        }
        return __saturating_time_add(
            std::chrono::steady_clock::now(),
            delay);
    }
};

class __callable {
public:
    __callable() noexcept = default;
    ~__callable() noexcept { reset(); }

    __callable(const __callable&) = delete;
    __callable& operator=(const __callable&) = delete;

    __callable(__callable&& other) noexcept
        : ptr_(std::exchange(other.ptr_, nullptr))
        , memory_(std::exchange(other.memory_, nullptr))
        , ops_(std::exchange(other.ops_, nullptr))
    {}

    auto operator=(__callable&& other) noexcept -> __callable& {
        if (this == &other) {
            return *this;
        }
        reset();
        ptr_ = std::exchange(other.ptr_, nullptr);
        memory_ = std::exchange(other.memory_, nullptr);
        ops_ = std::exchange(other.ops_, nullptr);
        return *this;
    }

    template<class F>
    [[nodiscard]] static auto make(std::pmr::memory_resource* memory, F&& fn)
        -> __callable {
        using fn_t = std::decay_t<F>;
        static_assert(std::is_nothrow_invocable_v<fn_t&>,
            "timer completion callables must be noexcept");
        static_assert(std::is_nothrow_destructible_v<fn_t>,
            "timer completion callable destructors must be noexcept");
        using model_t = __model<fn_t>;

        memory = normalize_memory_resource(memory);
        void* storage = memory->allocate(sizeof(model_t), alignof(model_t));
        try {
            std::construct_at(static_cast<model_t*>(storage), std::forward<F>(fn));
        } catch (...) {
            memory->deallocate(storage, sizeof(model_t), alignof(model_t));
            throw;
        }
        return __callable{storage, memory, &__ops_for<fn_t>};
    }

    void operator()() noexcept {
        if (ops_) {
            ops_->call(ptr_);
        }
    }

    void reset() noexcept {
        if (!ops_) {
            return;
        }
        ops_->destroy(ptr_, memory_);
        ptr_ = nullptr;
        memory_ = nullptr;
        ops_ = nullptr;
    }

private:
    struct __ops {
        void (*call)(void*) noexcept;
        void (*destroy)(void*, std::pmr::memory_resource*) noexcept;
    };

    template<class F>
    struct __model {
        explicit __model(F fn)
            : fn_(std::move(fn))
        {}

        F fn_;
    };

    template<class F>
    inline static constexpr __ops __ops_for{
        [](void* ptr) noexcept {
            static_cast<__model<F>*>(ptr)->fn_();
        },
        [](void* ptr, std::pmr::memory_resource* memory) noexcept {
            auto* model = static_cast<__model<F>*>(ptr);
            std::destroy_at(model);
            memory->deallocate(model, sizeof(__model<F>), alignof(__model<F>));
        }};

    __callable(
        void* ptr,
        std::pmr::memory_resource* memory,
        const __ops* ops) noexcept
        : ptr_(ptr)
        , memory_(memory)
        , ops_(ops)
    {}

    void* ptr_ = nullptr;
    std::pmr::memory_resource* memory_ = nullptr;
    const __ops* ops_ = nullptr;
};

struct __stop_callback_fn {
    std::weak_ptr<__state> state;
    std::weak_ptr<__item> item;

    void operator()() const noexcept;
};

struct __item {
    using callback_t =
        std::stop_callback_for_t<any_stop_token, __stop_callback_fn>;

    std::chrono::steady_clock::time_point deadline;
    any_stop_token stop_token;
    __callable complete_value;
    __callable complete_stopped;
    std::optional<callback_t> stop_callback;
    std::atomic<bool> stop_requested{false};
    std::atomic<bool> delivered{false};

    void request_stop() noexcept {
        stop_requested.store(true, std::memory_order_release);
    }

    [[nodiscard]] bool is_stop_requested() const noexcept {
        return stop_requested.load(std::memory_order_acquire);
    }

    void deliver_value() noexcept {
        stop_callback.reset();
        complete_value();
        delivered.store(true, std::memory_order_release);
    }

    void deliver_stopped() noexcept {
        stop_callback.reset();
        complete_stopped();
        delivered.store(true, std::memory_order_release);
    }

    // An abandoning operation destructor parks here until the worker has
    // finished deliver_*: the completion itself no-ops (done_ was claimed)
    // but the stop registration teardown must not race the destruction of
    // the receiver environment.
    void wait_delivered() const noexcept {
        while (!delivered.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
    }

    void install_stop_callback(
        std::weak_ptr<__state> state,
        std::weak_ptr<__item> self) {
        if (stop_token.stop_possible()) {
            stop_callback.emplace(
                stop_token,
                __stop_callback_fn{std::move(state), std::move(self)});
        }
    }
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

    __op(std::shared_ptr<__state> state, __when when, R rcvr)
        : state_(std::move(state))
        , when_(when)
        , data_(make_data(state_, std::move(rcvr)))
    {}

    ~__op();

    void start() & noexcept;

    static auto make_data(std::shared_ptr<__state> state, R rcvr)
        -> std::shared_ptr<__op_data<R>>;

    std::shared_ptr<__state> state_;
    __when when_;
    std::shared_ptr<__op_data<R>> data_;
    std::shared_ptr<__item> item_;
};

struct __sender {
    using sender_concept = std::execution::sender_t;

    std::shared_ptr<__state> state;
    __when when;

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
        return __op<R>{std::move(state), when, std::move(rcvr)};
    }

    template<std::execution::receiver R>
    auto connect(R rcvr) const& -> __op<R> {
        return __op<R>{state, when, std::move(rcvr)};
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

    void request_item_stop(const std::shared_ptr<__item>& item) noexcept {
        {
            std::lock_guard lk{mtx};
            if (item) {
                item->request_stop();
            }
        }
        cv.notify_all();
    }

    // Removes a still-queued item whose operation state is being destroyed.
    // Returns false when the worker already popped the item for delivery;
    // the caller then waits for that delivery to finish instead.
    [[nodiscard]] bool discard_item(
        const std::shared_ptr<__item>& item) noexcept {
        std::lock_guard lk{mtx};
        const auto it = std::find(items.begin(), items.end(), item);
        if (it == items.end()) {
            return false;
        }
        items.erase(it);
        --pending;
        if (pending == 0) {
            cv_wait.notify_all();
        }
        return true;
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
                            return item->is_stop_requested();
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

                    constexpr auto max_wait_slice = std::chrono::hours{24};
                    const auto remaining =
                        __saturating_time_difference<
                            std::chrono::steady_clock::duration>(
                            (*next_it)->deadline,
                            now);
                    cv.wait_for(lk, std::min(
                        remaining,
                        std::chrono::duration_cast<
                            std::chrono::steady_clock::duration>(
                            max_wait_slice)));
                }
            }

            if (ready) {
                if (complete_stopped) {
                    ready->deliver_stopped();
                } else {
                    ready->deliver_value();
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
        const auto steady_delay =
            __timer_detail::__saturating_nonnegative_duration_cast<
                std::chrono::steady_clock::duration>(delay);
        // The delay is anchored when the operation starts, not here, so a
        // sender that sits in a queue before start() still waits in full.
        return __timer_detail::__sender{
            state_,
            __timer_detail::__when::after(steady_delay)};
    }

    template<class Clock, class Duration>
    [[nodiscard]] auto schedule_at(std::chrono::time_point<Clock, Duration> time)
        -> __timer_detail::__sender {
        const auto now = Clock::now();
        const auto steady_now = std::chrono::steady_clock::now();
        // A naive time <= now comparison converts both sides through
        // common_type, which overflows the tick multiplication for
        // coarse-duration sentinels such as
        // time_point<Clock, seconds>::max(). The saturating wide
        // representation is the only safe ordering test here.
        const auto delay = __timer_detail::__saturating_time_difference<
            std::chrono::steady_clock::duration>(time, now);
        if (delay <= std::chrono::steady_clock::duration::zero()) {
            return schedule_at_steady(steady_now);
        }
        return schedule_at_steady(
            __timer_detail::__saturating_time_add(steady_now, delay));
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
        return __timer_detail::__sender{
            state_,
            __timer_detail::__when::at(deadline)};
    }

    std::shared_ptr<__timer_detail::__state> state_;
    std::thread thread_;
};

namespace __timer_detail {

inline void __stop_callback_fn::operator()() const noexcept {
    auto item_ptr = item.lock();
    auto state_ptr = state.lock();
    if (state_ptr) {
        state_ptr->request_item_stop(item_ptr);
    } else if (item_ptr) {
        item_ptr->request_stop();
    }
}

// Destroying a started-but-pending operation abandons the timer: the
// completion is claimed so the worker can never touch the receiver again,
// the queued item is discarded (or an in-flight delivery is waited out),
// and the stop registration is torn down before the receiver environment
// can go away.
template<class R>
inline __op<R>::~__op() {
    if (!item_ || !data_) {
        return;
    }
    if (data_->done_.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    if (state_ && state_->discard_item(item_)) {
        item_->stop_callback.reset();
        return;
    }
    item_->wait_delivered();
}

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
        auto* memory = state_ ? state_->memory : default_memory_resource();
        item_->deadline = when_.resolve_at_start();
        item_->complete_value = __callable::make(
            memory,
            [data] noexcept { data->complete_value(); });
        item_->complete_stopped = __callable::make(
            memory,
            [data] noexcept { data->complete_stopped(); });

        auto env = std::execution::get_env(data->rcvr_);
        auto token = std::execution::get_stop_token(env);
        if (token.stop_requested()) {
            data->complete_stopped();
            return;
        }
        if (token.stop_possible()) {
            item_->stop_token = any_stop_token{std::move(token)};
            item_->install_stop_callback(state_, item_);
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
