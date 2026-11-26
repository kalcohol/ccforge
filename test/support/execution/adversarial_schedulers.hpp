#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <execution>
#include <mutex>
#include <optional>
#include <span>
#include <thread>
#include <type_traits>
#include <utility>

namespace test_execution {

enum class schedule_kind { inline_immediate, never_resume, other_thread, random_delay, stop_at_every_step };

// Each replay selects at most one zero-based stop point. To sweep every
// point, use a fresh source and restart the scenario for each point.
struct schedule_options {
    schedule_kind kind;
    std::uint32_t seed = 0;
    std::inplace_stop_source* stop_source = nullptr;
    std::optional<std::size_t> stop_at = std::nullopt;
    // Optional bounded trace in random-delay dispatch order. The harness
    // keeps the storage alive and reads it only after settle().
    std::span<unsigned> worker_yield_counts{};
};

// A single-dispatcher fixture. The harness keeps this object, the stop
// source, and started operations alive through settle(). Completion can
// destroy its own operation, so dispatch never touches a node afterwards.
class adversarial_schedules {
    struct work {
        work* next = nullptr;
        void (*complete)(work*) noexcept;
        unsigned delay = 0;
    };

    template<class R>
    struct operation : work {
        using operation_state_concept = std::execution::operation_state_tag;

        operation(adversarial_schedules* driver, R&& receiver)
            noexcept(std::is_nothrow_move_constructible_v<R>)
            : work{nullptr, &dispatch}, driver(driver), receiver(std::move(receiver)) {}

        operation(const operation&) = delete;
        operation& operator=(const operation&) = delete;

        void start() & noexcept { driver->submit(this); }

        static void dispatch(work* item) noexcept {
            auto& rcvr = static_cast<operation*>(item)->receiver;
            auto token = std::get_stop_token(std::execution::get_env(rcvr));
            if constexpr (!std::unstoppable_token<decltype(token)>) {
                if (token.stop_requested()) {
                    std::execution::set_stopped(std::move(rcvr));
                    return;
                }
            }
            std::execution::set_value(std::move(rcvr));
        }

        adversarial_schedules* driver;
        R receiver;
    };

    struct schedule_sender;

public:
    struct scheduler {
        using scheduler_concept = std::execution::scheduler_tag;

        adversarial_schedules* driver;
        bool operator==(const scheduler&) const noexcept = default;
        auto schedule() const noexcept -> schedule_sender;

        auto query(std::execution::get_completion_scheduler_t<std::execution::set_value_t>)
            const noexcept -> scheduler { return *this; }

        auto query(std::execution::get_completion_scheduler_t<std::execution::set_stopped_t>)
            const noexcept -> scheduler { return *this; }

        auto query(std::execution::get_forward_progress_guarantee_t) const noexcept
            -> std::execution::forward_progress_guarantee {
            using enum std::execution::forward_progress_guarantee;
            // The worker modes own a dedicated thread. The other modes use
            // the caller of start() or rely on the harness to call settle().
            return driver->uses_worker() ? parallel : weakly_parallel;
        }
    };

    explicit adversarial_schedules(schedule_options options)
        : options_(options) {
        if (uses_worker()) worker_ = std::thread([this] { run(); });
    }

    adversarial_schedules(const adversarial_schedules&) = delete;
    adversarial_schedules& operator=(const adversarial_schedules&) = delete;

    ~adversarial_schedules() {
        if (worker_.joinable()) {
            {
                std::lock_guard lock{mutex_};
                closing_ = true;
            }
            ready_.notify_one();
            worker_.join();
        }
        // In never_resume, drain explicitly while the operations still live.
        if (head_) std::terminate();
    }

    auto get_scheduler() noexcept -> scheduler { return {this}; }
    std::size_t steps() const noexcept { return steps_.load(); }

    // Fixed integer mixing, independent of the standard library's random
    // distribution. Replays reproduce the number of yields, not OS timing.
    static constexpr unsigned delay_at(std::uint32_t seed, std::size_t step) noexcept {
        std::uint32_t bits = seed + static_cast<std::uint32_t>(step) * 0x9e3779b9u;
        bits = (bits ^ (bits >> 16)) * 0x7feb352du;
        bits ^= bits >> 15;
        return bits % 8;
    }

    // Harness thread only, after outside submitters have returned. Releases
    // parked work on that thread or waits for the worker's callbacks to return.
    // Further submissions can be made after this barrier.
    void settle() noexcept {
        std::unique_lock lock{mutex_};
        if (uses_worker()) {
            idle_.wait(lock, [this] { return !head_ && !active_; });
        } else {
            while (head_) {
                work* item = pop_locked();
                lock.unlock();
                item->complete(item);
                lock.lock();
            }
        }
    }

private:
    struct schedule_sender {
        using sender_concept = std::execution::sender_tag;
        adversarial_schedules* driver;

        auto get_env() const noexcept -> scheduler { return {driver}; }

        template<class Self, class Env>
        static constexpr auto get_completion_signatures() noexcept {
            if constexpr (std::unstoppable_token<std::stop_token_of_t<Env>>) {
                return std::execution::completion_signatures<std::execution::set_value_t()>{};
            } else {
                return std::execution::completion_signatures<
                    std::execution::set_value_t(), std::execution::set_stopped_t()>{};
            }
        }

        template<std::execution::receiver R>
        auto connect(R receiver) const noexcept(std::is_nothrow_move_constructible_v<R>)
            -> operation<R> {
            return {driver, std::move(receiver)};
        }
    };

    bool uses_worker() const noexcept {
        return options_.kind == schedule_kind::other_thread ||
               options_.kind == schedule_kind::random_delay;
    }

    void submit(work* item) noexcept {
        const auto step = steps_.fetch_add(1);
        if (options_.kind == schedule_kind::inline_immediate ||
            options_.kind == schedule_kind::stop_at_every_step) {
            if (options_.kind == schedule_kind::stop_at_every_step &&
                options_.stop_source && options_.stop_at == step) {
                options_.stop_source->request_stop();
            }
            item->complete(item);
            return;
        }

        if (options_.kind == schedule_kind::random_delay) {
            item->delay = delay_at(options_.seed, step);
        }
        {
            std::lock_guard lock{mutex_};
            if (tail_) tail_->next = item;
            else head_ = item;
            tail_ = item;
        }
        if (uses_worker()) ready_.notify_one();
    }

    work* pop_locked() noexcept {
        work* item = head_;
        head_ = item->next;
        if (!head_) tail_ = nullptr;
        return item;
    }

    static void counted_yield(unsigned& consumed) noexcept {
        std::this_thread::yield();
        ++consumed;
    }

    void run() noexcept {
        auto yield_counts = options_.kind == schedule_kind::random_delay
            ? options_.worker_yield_counts : std::span<unsigned>{};
        std::unique_lock lock{mutex_};
        for (;;) {
            ready_.wait(lock, [this] { return head_ || closing_; });
            if (!head_) return;
            work* item = pop_locked();
            active_ = true;
            lock.unlock();
            unsigned consumed = 0;
            for (unsigned delay = item->delay; delay != 0; --delay) {
                counted_yield(consumed);
            }
            if (!yield_counts.empty()) {
                yield_counts.front() = consumed;
                yield_counts = yield_counts.subspan(1);
            }
            item->complete(item);
            lock.lock();
            active_ = false;
            if (!head_) idle_.notify_all();
        }
    }

    const schedule_options options_;
    std::atomic<std::size_t> steps_{0};
    std::mutex mutex_;
    std::condition_variable ready_, idle_;
    work* head_ = nullptr;
    work* tail_ = nullptr;
    bool active_ = false;
    bool closing_ = false;
    std::thread worker_;
};

inline auto adversarial_schedules::scheduler::schedule() const noexcept -> schedule_sender {
    return {driver};
}

} // namespace test_execution
