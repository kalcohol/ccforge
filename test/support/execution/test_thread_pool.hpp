#pragma once

#include <condition_variable>
#include <cstddef>
#include <execution>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace test_execution {

// Schedulers and senders borrow the pool. The owner must keep it alive for
// submissions, and each accepted operation must live until completion starts.
class test_thread_pool {
    struct node {
        node* next = nullptr;
        void (*complete)(node*) noexcept;
    };

    template<class R>
    struct operation : node {
        using operation_state_concept = std::execution::operation_state_tag;

        operation(test_thread_pool* pool, R&& receiver)
            : node{nullptr, &complete_value}, pool(pool), receiver(std::move(receiver)) {}

        operation(const operation&) = delete;
        operation& operator=(const operation&) = delete;

        void start() & noexcept {
            if (!pool) {
                std::execution::set_value(std::move(receiver));
                return;
            }
            // On acceptance a worker can destroy *this before enqueue returns.
            if (!pool->enqueue(this)) {
                std::execution::set_stopped(std::move(receiver));
            }
        }

        static void complete_value(node* work) noexcept {
            std::execution::set_value(std::move(static_cast<operation*>(work)->receiver));
        }

        test_thread_pool* pool;
        R receiver;
    };

    struct schedule_sender;

public:
    struct scheduler {
        using scheduler_concept = std::execution::scheduler_tag;
        // A null pool denotes the inline resource: schedule runs on the starter.
        test_thread_pool* pool;

        bool operator==(const scheduler&) const noexcept = default;
        auto schedule() const noexcept -> schedule_sender;

        auto query(std::execution::get_completion_scheduler_t<std::execution::set_value_t>)
            const noexcept -> scheduler {
            return *this;
        }

        auto query(std::execution::get_completion_scheduler_t<std::execution::set_stopped_t>)
            const noexcept -> scheduler {
            // Rejection runs inline. Without this answer an environment-aware
            // query on the scheduler-valued attributes would fall back to the pool.
            return {nullptr};
        }

        auto query(std::execution::get_forward_progress_guarantee_t) const noexcept {
            // Running work can occupy every worker while other work is queued.
            return std::execution::forward_progress_guarantee::weakly_parallel;
        }
    };

    explicit test_thread_pool(std::size_t worker_count) {
        if (worker_count == 0) throw std::invalid_argument("test pool needs a worker");
        workers_.reserve(worker_count);
        try {
            for (std::size_t i = 0; i < worker_count; ++i) {
                workers_.emplace_back([this] { run(); });
            }
        } catch (...) {
            join();
            throw;
        }
    }

    test_thread_pool(const test_thread_pool&) = delete;
    test_thread_pool& operator=(const test_thread_pool&) = delete;
    ~test_thread_pool() { join(); }

    auto get_scheduler() noexcept -> scheduler { return {this}; }

    // Stop accepting work without waiting for workers. Accepted work still
    // completes on a worker. A start after close completes stopped inline.
    void close() noexcept {
        {
            std::lock_guard lock{mutex_};
            closed_ = true;
        }
        ready_.notify_all();
    }

    // Owner only, never from a worker. Close, drain accepted work and join.
    void join() noexcept {
        close();
        for (auto& worker : workers_) {
            if (worker.joinable()) worker.join();
        }
    }

private:
    struct schedule_sender {
        using sender_concept = std::execution::sender_tag;
        test_thread_pool* pool;

        auto get_env() const noexcept -> scheduler { return {pool}; }

        template<class Self, class... Env>
        static constexpr auto get_completion_signatures() noexcept
            -> std::execution::completion_signatures<
                std::execution::set_value_t(), std::execution::set_stopped_t()> {
            return {};
        }

        template<std::execution::receiver R>
        auto connect(R receiver) const -> operation<R> {
            return {pool, std::move(receiver)};
        }
    };

    bool enqueue(node* work) noexcept {
        {
            std::lock_guard lock{mutex_};
            if (closed_) return false;
            if (tail_) tail_->next = work;
            else head_ = work;
            tail_ = work;
        }
        ready_.notify_one();
        return true;
    }

    void run() noexcept {
        for (;;) {
            node* work;
            {
                std::unique_lock lock{mutex_};
                ready_.wait(lock, [this] { return head_ || closed_; });
                if (!head_) return;
                work = head_;
                head_ = work->next;
                if (!head_) tail_ = nullptr;
            }
            // Unlinked before dispatch. The receiver can delete the operation
            // during completion, so nothing below uses the node again.
            work->complete(work);
        }
    }

    std::mutex mutex_;
    std::condition_variable ready_;
    node* head_ = nullptr;
    node* tail_ = nullptr;
    bool closed_ = false;
    std::vector<std::thread> workers_;
};

inline auto test_thread_pool::scheduler::schedule() const noexcept -> schedule_sender {
    return {pool};
}

} // namespace test_execution
