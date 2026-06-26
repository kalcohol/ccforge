#include <gtest/gtest.h>

#include <forge/io/combinators.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <execution>
#include <memory>
#include <mutex>
#include <optional>
#include <system_error>
#include <thread>

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L

namespace {

namespace cio = forge::io;
using namespace std::chrono_literals;

using child_result = cio::io_result<std::size_t>;
using pair_payload = cio::when_all_result<child_result, child_result>;
using aggregate_result = cio::io_result<pair_payload>;

enum class controlled_completion {
    value,
    eof,
    error,
    stopped
};

struct controlled_state {
    std::mutex mtx;
    std::condition_variable cv;
    bool started = false;
    bool release = false;
    bool returned = false;
    bool stop_completes = true;
    int stop_attempts = 0;
    controlled_completion completion = controlled_completion::value;
    child_result result = child_result::success(0);
};

auto wait_started(const std::shared_ptr<controlled_state>& state) -> void {
    std::unique_lock lock{state->mtx};
    state->cv.wait(lock, [&] { return state->started; });
}

auto wait_returned(const std::shared_ptr<controlled_state>& state) -> void {
    std::unique_lock lock{state->mtx};
    state->cv.wait(lock, [&] { return state->returned; });
}

auto release_child(
    const std::shared_ptr<controlled_state>& state,
    controlled_completion completion,
    child_result result = child_result::success(0)) -> void {
    {
        std::lock_guard lock{state->mtx};
        state->completion = completion;
        state->result = std::move(result);
        state->release = true;
    }
    state->cv.notify_all();
}

template<class R>
struct controlled_delivery {
    struct stop_callback {
        std::weak_ptr<controlled_delivery> self;

        auto operator()() const noexcept -> void {
            if (auto shared = self.lock()) {
                shared->complete_stopped_from_stop();
            }
        }
    };

    using env_t = std::execution::env_of_t<R>;
    using token_t = decltype(std::execution::get_stop_token(std::declval<env_t>()));
    using callback_t = std::stop_callback_for_t<token_t, stop_callback>;

    R receiver;
    std::shared_ptr<controlled_state> state;
    std::optional<callback_t> callback;
    std::atomic<bool> done{false};

    controlled_delivery(R rcvr, std::shared_ptr<controlled_state> st)
        : receiver(std::move(rcvr))
        , state(std::move(st))
    {}

    auto complete_stopped_from_stop() noexcept -> void {
        bool should_complete = false;
        {
            std::lock_guard lock{state->mtx};
            ++state->stop_attempts;
            should_complete = state->stop_completes;
        }
        if (should_complete) {
            complete_stopped(false);
        } else {
            state->cv.notify_all();
        }
    }

    auto complete_stopped(bool reset_callback = true) noexcept -> void {
        if (done.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        if (reset_callback) {
            callback.reset();
        }
        std::execution::set_stopped(std::move(receiver));
        {
            std::lock_guard lock{state->mtx};
            state->returned = true;
        }
        state->cv.notify_all();
    }

    auto complete_value(child_result result) noexcept -> void {
        if (done.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        callback.reset();
        std::execution::set_value(std::move(receiver), std::move(result));
        {
            std::lock_guard lock{state->mtx};
            state->returned = true;
        }
        state->cv.notify_all();
    }
};

template<class R>
struct controlled_op {
    using operation_state_concept = std::execution::operation_state_t;

    std::shared_ptr<controlled_delivery<R>> delivery;

    controlled_op(R receiver, std::shared_ptr<controlled_state> state)
        : delivery(std::make_shared<controlled_delivery<R>>(
              std::move(receiver),
              std::move(state)))
    {}

    controlled_op(controlled_op&&) = delete;
    auto operator=(controlled_op&&) -> controlled_op& = delete;
    controlled_op(const controlled_op&) = delete;
    auto operator=(const controlled_op&) -> controlled_op& = delete;

    ~controlled_op() {
        if (delivery) {
            {
                std::lock_guard lock{delivery->state->mtx};
                delivery->state->release = true;
            }
            delivery->state->cv.notify_all();
        }
    }

    auto start() & noexcept -> void {
        try {
            auto token = std::execution::get_stop_token(
                std::execution::get_env(delivery->receiver));
            {
                std::lock_guard lock{delivery->state->mtx};
                delivery->state->started = true;
            }
            delivery->state->cv.notify_all();

            if (token.stop_requested()) {
                delivery->complete_stopped_from_stop();
                return;
            }
            if (token.stop_possible()) {
                delivery->callback.emplace(
                    token,
                    typename controlled_delivery<R>::stop_callback{
                        delivery});
            }

            auto shared = delivery;
            std::thread([shared] {
                controlled_completion completion;
                child_result result;
                {
                    std::unique_lock lock{shared->state->mtx};
                    shared->state->cv.wait(
                        lock,
                        [&] { return shared->state->release; });
                    completion = shared->state->completion;
                    result = std::move(shared->state->result);
                }

                switch (completion) {
                case controlled_completion::value:
                case controlled_completion::eof:
                case controlled_completion::error:
                    shared->complete_value(std::move(result));
                    break;
                case controlled_completion::stopped:
                    shared->complete_stopped();
                    break;
                }
            }).detach();
        } catch (...) {
            std::execution::set_error(
                std::move(delivery->receiver),
                std::current_exception());
        }
    }
};

struct controlled_sender {
    using sender_concept = std::execution::sender_t;

    std::shared_ptr<controlled_state> state;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<
            std::execution::set_value_t(child_result),
            std::execution::set_error_t(std::exception_ptr),
            std::execution::set_stopped_t()> {
        return {};
    }

    [[nodiscard]] auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }

    template<std::execution::receiver R>
    auto connect(R receiver) && -> controlled_op<R> {
        return controlled_op<R>{std::move(receiver), std::move(state)};
    }
};

auto child_task(std::shared_ptr<controlled_state> state)
    -> cio::io_task<child_result> {
    auto [result] = co_await cio::await_sender(
        controlled_sender{std::move(state)});
    co_return std::move(result);
}

struct aggregate_state {
    std::mutex mtx;
    std::condition_variable cv;
    bool done = false;
    int completions = 0;
    std::optional<aggregate_result> value;
    std::exception_ptr error;
    bool stopped = false;
};

struct aggregate_receiver {
    using receiver_concept = std::execution::receiver_t;

    struct env {
        std::inplace_stop_source* stop = nullptr;

        friend auto tag_invoke(
            std::execution::get_stop_token_t,
            const env& self) noexcept -> std::inplace_stop_token {
            return self.stop ? self.stop->get_token() : std::inplace_stop_token{};
        }
    };

    std::shared_ptr<aggregate_state> state;
    std::inplace_stop_source* stop = nullptr;

    auto set_value(aggregate_result result) && noexcept -> void {
        {
            std::lock_guard lock{state->mtx};
            ++state->completions;
            state->value.emplace(std::move(result));
            state->done = true;
        }
        state->cv.notify_all();
    }

    auto set_error(std::exception_ptr error) && noexcept -> void {
        {
            std::lock_guard lock{state->mtx};
            ++state->completions;
            state->error = std::move(error);
            state->done = true;
        }
        state->cv.notify_all();
    }

    auto set_stopped() && noexcept -> void {
        {
            std::lock_guard lock{state->mtx};
            ++state->completions;
            state->stopped = true;
            state->done = true;
        }
        state->cv.notify_all();
    }

    [[nodiscard]] auto get_env() const noexcept -> env {
        return env{stop};
    }
};

auto wait_done(const std::shared_ptr<aggregate_state>& state) -> void {
    std::unique_lock lock{state->mtx};
    ASSERT_TRUE(state->cv.wait_for(lock, 1s, [&] { return state->done; }));
}

struct fixture {
    std::shared_ptr<controlled_state> first =
        std::make_shared<controlled_state>();
    std::shared_ptr<controlled_state> second =
        std::make_shared<controlled_state>();
    std::shared_ptr<aggregate_state> aggregate =
        std::make_shared<aggregate_state>();

    [[nodiscard]] auto connect() {
        return std::execution::connect(
            cio::when_all_results(
                child_task(first),
                child_task(second)),
            aggregate_receiver{aggregate});
    }

    auto start(auto& op) -> void {
        std::execution::start(op);
        wait_started(first);
        wait_started(second);
    }
};

} // namespace

TEST(ForgeIoCombinatorsTest, WhenAllResultsCombinesTwoValues) {
    fixture f;
    auto op = f.connect();
    f.start(op);

    release_child(
        f.first,
        controlled_completion::value,
        child_result::success(3));
    release_child(
        f.second,
        controlled_completion::value,
        child_result::success(7));

    wait_done(f.aggregate);

    std::lock_guard lock{f.aggregate->mtx};
    ASSERT_EQ(f.aggregate->completions, 1);
    ASSERT_TRUE(f.aggregate->value.has_value());
    EXPECT_TRUE(*f.aggregate->value);
    auto [error, payload] = *f.aggregate->value;
    EXPECT_FALSE(error);
    ASSERT_TRUE(payload.first.has_value());
    ASSERT_TRUE(payload.second.has_value());
    EXPECT_EQ(cio::get<1>(*payload.first), 3u);
    EXPECT_EQ(cio::get<1>(*payload.second), 7u);
    EXPECT_FALSE(f.aggregate->stopped);
    EXPECT_FALSE(f.aggregate->error);
}

TEST(ForgeIoCombinatorsTest, WhenAllResultsPreservesValueBeforeEof) {
    fixture f;
    auto op = f.connect();
    f.start(op);

    release_child(
        f.first,
        controlled_completion::value,
        child_result::success(3));
    wait_returned(f.first);
    release_child(
        f.second,
        controlled_completion::eof,
        child_result::end_of_file(5));

    wait_done(f.aggregate);

    std::lock_guard lock{f.aggregate->mtx};
    ASSERT_EQ(f.aggregate->completions, 1);
    ASSERT_TRUE(f.aggregate->value.has_value());
    EXPECT_TRUE(f.aggregate->value->eof());
    auto [error, payload] = *f.aggregate->value;
    EXPECT_FALSE(error);
    ASSERT_TRUE(payload.first.has_value());
    ASSERT_TRUE(payload.second.has_value());
    EXPECT_EQ(cio::get<1>(*payload.first), 3u);
    EXPECT_EQ(cio::get<1>(*payload.second), 5u);
}

TEST(ForgeIoCombinatorsTest, WhenAllResultsErrorWinsOverPriorValue) {
    fixture f;
    auto op = f.connect();
    f.start(op);

    release_child(
        f.first,
        controlled_completion::value,
        child_result::success(11));
    wait_returned(f.first);
    release_child(
        f.second,
        controlled_completion::error,
        child_result::failure(
            std::make_error_code(std::errc::connection_reset),
            4));

    wait_done(f.aggregate);

    std::lock_guard lock{f.aggregate->mtx};
    ASSERT_EQ(f.aggregate->completions, 1);
    ASSERT_TRUE(f.aggregate->value.has_value());
    EXPECT_EQ(
        f.aggregate->value->status(),
        cio::io_status::error);
    auto [error, payload] = *f.aggregate->value;
    EXPECT_EQ(error, std::make_error_code(std::errc::connection_reset));
    ASSERT_TRUE(payload.first.has_value());
    ASSERT_TRUE(payload.second.has_value());
    EXPECT_EQ(cio::get<1>(*payload.first), 11u);
    EXPECT_EQ(cio::get<1>(*payload.second), 4u);
}

TEST(ForgeIoCombinatorsTest, WhenAllResultsErrorWinsOverPriorEof) {
    fixture f;
    f.second->stop_completes = false;
    auto op = f.connect();
    f.start(op);

    release_child(
        f.first,
        controlled_completion::eof,
        child_result::end_of_file(2));
    wait_returned(f.first);
    release_child(
        f.second,
        controlled_completion::error,
        child_result::failure(
            std::make_error_code(std::errc::broken_pipe),
            8));

    wait_done(f.aggregate);

    std::lock_guard lock{f.aggregate->mtx};
    ASSERT_EQ(f.aggregate->completions, 1);
    ASSERT_TRUE(f.aggregate->value.has_value());
    auto [error, payload] = *f.aggregate->value;
    EXPECT_EQ(error, std::make_error_code(std::errc::broken_pipe));
    ASSERT_TRUE(payload.first.has_value());
    ASSERT_TRUE(payload.second.has_value());
    EXPECT_TRUE(payload.first->eof());
    EXPECT_EQ(cio::get<1>(*payload.first), 2u);
    EXPECT_EQ(cio::get<1>(*payload.second), 8u);
}

TEST(ForgeIoCombinatorsTest, WhenAllResultsErrorStopsPendingSibling) {
    fixture f;
    auto op = f.connect();
    f.start(op);

    release_child(
        f.first,
        controlled_completion::error,
        child_result::failure(
            std::make_error_code(std::errc::connection_reset),
            6));

    wait_done(f.aggregate);

    std::lock_guard aggregate_lock{f.aggregate->mtx};
    ASSERT_EQ(f.aggregate->completions, 1);
    ASSERT_TRUE(f.aggregate->value.has_value());
    auto [error, payload] = *f.aggregate->value;
    EXPECT_EQ(error, std::make_error_code(std::errc::connection_reset));
    ASSERT_TRUE(payload.first.has_value());
    EXPECT_FALSE(payload.second.has_value());
    EXPECT_EQ(cio::get<1>(*payload.first), 6u);
    EXPECT_FALSE(f.aggregate->stopped);

    std::lock_guard second_lock{f.second->mtx};
    EXPECT_EQ(f.second->stop_attempts, 1);
}

TEST(ForgeIoCombinatorsTest, WhenAllResultsStopsWhenChildStopsFirst) {
    fixture f;
    auto op = f.connect();
    f.start(op);

    release_child(f.first, controlled_completion::stopped);

    wait_done(f.aggregate);

    std::lock_guard aggregate_lock{f.aggregate->mtx};
    EXPECT_EQ(f.aggregate->completions, 1);
    EXPECT_TRUE(f.aggregate->stopped);
    EXPECT_FALSE(f.aggregate->value.has_value());
    EXPECT_FALSE(f.aggregate->error);

    std::lock_guard second_lock{f.second->mtx};
    EXPECT_EQ(f.second->stop_attempts, 1);
}

TEST(ForgeIoCombinatorsTest, WhenAllResultsDownstreamStopCancelsChildren) {
    fixture f;
    std::inplace_stop_source downstream_stop;
    auto op = std::execution::connect(
        cio::when_all_results(
            child_task(f.first),
            child_task(f.second)),
        aggregate_receiver{f.aggregate, &downstream_stop});
    f.start(op);

    downstream_stop.request_stop();

    wait_done(f.aggregate);

    std::lock_guard aggregate_lock{f.aggregate->mtx};
    EXPECT_EQ(f.aggregate->completions, 1);
    EXPECT_TRUE(f.aggregate->stopped);
    EXPECT_FALSE(f.aggregate->value.has_value());
    EXPECT_FALSE(f.aggregate->error);

    std::lock_guard first_lock{f.first->mtx};
    EXPECT_EQ(f.first->stop_attempts, 1);

    std::lock_guard second_lock{f.second->mtx};
    EXPECT_EQ(f.second->stop_attempts, 1);
}

TEST(ForgeIoCombinatorsTest, WhenAllResultsRaceErrorAndValueCompletesOnce) {
    constexpr int iterations = 64;
    for (int i = 0; i < iterations; ++i) {
        fixture f;
        f.first->stop_completes = false;
        f.second->stop_completes = false;
        auto op = f.connect();
        f.start(op);
        std::atomic<bool> go{false};

        std::thread error_thread([&] {
            while (!go.load(std::memory_order_acquire)) {}
            release_child(
                f.first,
                controlled_completion::error,
                child_result::failure(
                    std::make_error_code(std::errc::connection_reset),
                    1));
        });
        std::thread value_thread([&] {
            while (!go.load(std::memory_order_acquire)) {}
            release_child(
                f.second,
                controlled_completion::value,
                child_result::success(2));
        });

        go.store(true, std::memory_order_release);
        error_thread.join();
        value_thread.join();

        wait_done(f.aggregate);

        std::lock_guard lock{f.aggregate->mtx};
        ASSERT_EQ(f.aggregate->completions, 1);
        ASSERT_TRUE(f.aggregate->value.has_value());
        auto [error, payload] = *f.aggregate->value;
        EXPECT_EQ(error, std::make_error_code(std::errc::connection_reset));
        ASSERT_TRUE(payload.first.has_value());
        ASSERT_TRUE(payload.second.has_value());
    }
}

#else

TEST(ForgeIoCombinatorsTest, CoroutinesNotAvailable) {
    GTEST_SKIP() << "C++20 coroutines not available on this toolchain";
}

#endif
