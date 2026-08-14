#include <gtest/gtest.h>

#include <forge/io/combinators.hpp>
#include "forge_operation_destroy.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <execution>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <utility>

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L

namespace {

namespace cio = forge::io;
using namespace std::chrono_literals;

using child_result = cio::io_result<std::size_t>;
using pair_payload = cio::when_all_result<child_result, child_result>;
using aggregate_result = cio::io_result<pair_payload>;

struct throwing_move_control {
    bool throw_on_move = false;
};

struct throwing_move_payload {
    std::shared_ptr<throwing_move_control> control;
    int value = 0;

    throwing_move_payload(
        std::shared_ptr<throwing_move_control> state,
        int input) noexcept
        : control(std::move(state)), value(input) {}

    throwing_move_payload(throwing_move_payload&& other) noexcept(false)
        : control(other.control), value(other.value) {
        if (control && control->throw_on_move) {
            throw std::runtime_error{"injected payload move failure"};
        }
    }

    auto operator=(throwing_move_payload&&) -> throwing_move_payload& = delete;
    throwing_move_payload(const throwing_move_payload&) = delete;
    auto operator=(const throwing_move_payload&)
        -> throwing_move_payload& = delete;
};

using throwing_child_result = cio::io_result<throwing_move_payload>;
using throwing_pair_payload =
    cio::when_all_result<throwing_child_result, throwing_child_result>;
using throwing_aggregate_result = cio::io_result<throwing_pair_payload>;

struct throwing_aggregate_state {
    int completions = 0;
    bool value = false;
    bool error = false;
    bool stopped = false;
    int first = 0;
    int second = 0;
};

struct throwing_aggregate_receiver {
    using receiver_concept = std::execution::receiver_t;

    std::shared_ptr<throwing_aggregate_state> state;

    auto set_value(throwing_aggregate_result&& result) && noexcept -> void {
        ++state->completions;
        state->value = true;
        auto& payload = cio::get<1>(result);
        if (payload.first) {
            state->first = cio::get<1>(*payload.first).value;
        }
        if (payload.second) {
            state->second = cio::get<1>(*payload.second).value;
        }
    }

    auto set_error(std::exception_ptr) && noexcept -> void {
        ++state->completions;
        state->error = true;
    }

    auto set_stopped() && noexcept -> void {
        ++state->completions;
        state->stopped = true;
    }

    [[nodiscard]] auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }
};

auto throwing_payload_task(
    std::shared_ptr<throwing_move_control> control,
    int value) -> cio::io_task<throwing_child_result> {
    co_return throwing_child_result::success(
        throwing_move_payload{std::move(control), value});
}

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
    bool reset_callback_before_completion = true;
    bool block_stop = false;
    bool stop_entered = false;
    bool release_stop = false;
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

auto wait_stop_entered(const std::shared_ptr<controlled_state>& state) -> void {
    std::unique_lock lock{state->mtx};
    state->cv.wait(lock, [&] { return state->stop_entered; });
}

auto release_stop_callback(const std::shared_ptr<controlled_state>& state) -> void {
    {
        std::lock_guard lock{state->mtx};
        state->release_stop = true;
    }
    state->cv.notify_all();
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
            std::unique_lock lock{state->mtx};
            ++state->stop_attempts;
            state->stop_entered = true;
            state->cv.notify_all();
            state->cv.wait(lock, [&] {
                return !state->block_stop || state->release_stop;
            });
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
        if (state->reset_callback_before_completion) {
            callback.reset();
        }
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

struct self_destroying_aggregate_receiver {
    using receiver_concept = std::execution::receiver_t;

    forge_test::destroy_context_base* context = nullptr;
    std::shared_ptr<aggregate_state> state;

    auto set_value(aggregate_result result) && noexcept -> void {
        {
            std::lock_guard lock{state->mtx};
            ++state->completions;
            state->value.emplace(std::move(result));
        }
        context->destroy();
        {
            std::lock_guard lock{state->mtx};
            state->done = true;
        }
        state->cv.notify_all();
    }

    auto set_error(std::exception_ptr error) && noexcept -> void {
        {
            std::lock_guard lock{state->mtx};
            ++state->completions;
            state->error = std::move(error);
        }
        context->destroy();
        {
            std::lock_guard lock{state->mtx};
            state->done = true;
        }
        state->cv.notify_all();
    }

    auto set_stopped() && noexcept -> void {
        {
            std::lock_guard lock{state->mtx};
            ++state->completions;
            state->stopped = true;
        }
        context->destroy();
        {
            std::lock_guard lock{state->mtx};
            state->done = true;
        }
        state->cv.notify_all();
    }

    [[nodiscard]] auto get_env() const noexcept -> std::execution::empty_env {
        return {};
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

using when_all_sender_t = decltype(cio::when_all_results(
    child_task(std::declval<std::shared_ptr<controlled_state>>()),
    child_task(std::declval<std::shared_ptr<controlled_state>>())));
using when_all_op_t = std::execution::connect_result_t<
    when_all_sender_t,
    aggregate_receiver>;
static_assert(std::execution::operation_state<when_all_op_t>);

} // namespace

TEST(ForgeCoroCombinatorsTest, WhenAllResultsAcceptsNonAssignableThrowingMovePayload) {
    auto first_control = std::make_shared<throwing_move_control>();
    auto second_control = std::make_shared<throwing_move_control>();
    auto state = std::make_shared<throwing_aggregate_state>();

    auto op = std::execution::connect(
        cio::when_all_results(
            throwing_payload_task(first_control, 3),
            throwing_payload_task(second_control, 7)),
        throwing_aggregate_receiver{state});
    std::execution::start(op);

    EXPECT_EQ(state->completions, 1);
    EXPECT_TRUE(state->value);
    EXPECT_FALSE(state->error);
    EXPECT_FALSE(state->stopped);
    EXPECT_EQ(state->first, 3);
    EXPECT_EQ(state->second, 7);
}

TEST(ForgeCoroCombinatorsTest, WhenAllResultsReportsPayloadMoveFailure) {
    using state_t = cio::__io_combinator_detail::shared_state<
        throwing_child_result,
        throwing_child_result,
        throwing_aggregate_receiver>;

    auto first_control = std::make_shared<throwing_move_control>();
    auto second_control = std::make_shared<throwing_move_control>();
    auto observed = std::make_shared<throwing_aggregate_state>();
    auto state = std::make_shared<state_t>(
        throwing_aggregate_receiver{observed});
    auto first = throwing_child_result::success(
        throwing_move_payload{first_control, 3});
    auto second = throwing_child_result::success(
        throwing_move_payload{second_control, 7});

    first_control->throw_on_move = true;
    state->template set_value<0>(std::move(first));
    state->template set_value<1>(std::move(second));

    EXPECT_EQ(observed->completions, 1);
    EXPECT_FALSE(observed->value);
    EXPECT_TRUE(observed->error);
    EXPECT_FALSE(observed->stopped);
}

TEST(ForgeCoroCombinatorsTest, WhenAllResultsCombinesTwoValues) {
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

TEST(ForgeCoroCombinatorsTest, WhenAllResultsPreservesValueBeforeEof) {
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

TEST(ForgeCoroCombinatorsTest, WhenAllResultsErrorWinsOverPriorValue) {
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

TEST(ForgeCoroCombinatorsTest, WhenAllResultsErrorWinsOverPriorEof) {
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

TEST(ForgeCoroCombinatorsTest, WhenAllResultsErrorStopsPendingSibling) {
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

TEST(ForgeCoroCombinatorsTest, WhenAllResultsStopsWhenChildStopsFirst) {
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

TEST(ForgeCoroCombinatorsTest, WhenAllResultsDownstreamStopCancelsChildren) {
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

TEST(ForgeCoroCombinatorsTest, WhenAllResultsAllowsReceiverToDestroyOperation) {
    fixture f;
    using sender_t = decltype(cio::when_all_results(
        child_task(f.first),
        child_task(f.second)));
    using receiver_t = self_destroying_aggregate_receiver;
    using op_t = decltype(std::execution::connect(
        std::declval<sender_t>(),
        std::declval<receiver_t>()));

    bool destroyed = false;
    forge_test::operation_destroy_context<op_t> context{&destroyed};

    auto& op = context.emplace_from([&] {
        return std::execution::connect(
            cio::when_all_results(
                child_task(f.first),
                child_task(f.second)),
            self_destroying_aggregate_receiver{&context, f.aggregate});
    });
    std::execution::start(op);

    release_child(
        f.first,
        controlled_completion::value,
        child_result::success(3));
    release_child(
        f.second,
        controlled_completion::value,
        child_result::success(5));

    wait_done(f.aggregate);
    EXPECT_TRUE(destroyed);
    EXPECT_FALSE(context.has_value);

    std::lock_guard lock{f.aggregate->mtx};
    ASSERT_EQ(f.aggregate->completions, 1);
    ASSERT_TRUE(f.aggregate->value.has_value());
    auto [error, payload] = *f.aggregate->value;
    EXPECT_FALSE(error);
    ASSERT_TRUE(payload.first.has_value());
    ASSERT_TRUE(payload.second.has_value());
    EXPECT_EQ(cio::get<1>(*payload.first), 3u);
    EXPECT_EQ(cio::get<1>(*payload.second), 5u);
}

TEST(ForgeCoroCombinatorsTest, WhenAllResultsAllowsSelfDestroyDuringSiblingStop) {
    fixture f;
    f.second->stop_completes = true;
    using sender_t = decltype(cio::when_all_results(
        child_task(f.first),
        child_task(f.second)));
    using receiver_t = self_destroying_aggregate_receiver;
    using op_t = decltype(std::execution::connect(
        std::declval<sender_t>(),
        std::declval<receiver_t>()));

    bool destroyed = false;
    forge_test::operation_destroy_context<op_t> context{&destroyed};

    auto& op = context.emplace_from([&] {
        return std::execution::connect(
            cio::when_all_results(
                child_task(f.first),
                child_task(f.second)),
            self_destroying_aggregate_receiver{&context, f.aggregate});
    });
    std::execution::start(op);

    release_child(
        f.first,
        controlled_completion::error,
        child_result::failure(
            std::make_error_code(std::errc::connection_reset),
            6));

    wait_done(f.aggregate);
    EXPECT_TRUE(destroyed);
    EXPECT_FALSE(context.has_value);

    {
        std::lock_guard second_lock{f.second->mtx};
        EXPECT_EQ(f.second->stop_attempts, 1);
    }

    std::lock_guard aggregate_lock{f.aggregate->mtx};
    ASSERT_EQ(f.aggregate->completions, 1);
    ASSERT_TRUE(f.aggregate->value.has_value());
    auto [error, payload] = *f.aggregate->value;
    EXPECT_EQ(error, std::make_error_code(std::errc::connection_reset));
    ASSERT_TRUE(payload.first.has_value());
    EXPECT_FALSE(payload.second.has_value());
    EXPECT_EQ(cio::get<1>(*payload.first), 6u);
}

TEST(ForgeCoroCombinatorsTest, SiblingStopFinishesBeforeCrossThreadDelivery) {
    fixture f;
    f.second->stop_completes = false;
    f.second->block_stop = true;
    f.second->reset_callback_before_completion = false;
    using sender_t = decltype(cio::when_all_results(
        child_task(f.first),
        child_task(f.second)));
    using receiver_t = self_destroying_aggregate_receiver;
    using op_t = decltype(std::execution::connect(
        std::declval<sender_t>(),
        std::declval<receiver_t>()));

    bool destroyed = false;
    forge_test::operation_destroy_context<op_t> context{&destroyed};
    auto& op = context.emplace_from([&] {
        return std::execution::connect(
            cio::when_all_results(
                child_task(f.first),
                child_task(f.second)),
            self_destroying_aggregate_receiver{&context, f.aggregate});
    });
    std::execution::start(op);

    release_child(
        f.first,
        controlled_completion::error,
        child_result::failure(
            std::make_error_code(std::errc::connection_reset),
            6));
    wait_stop_entered(f.second);

    release_child(
        f.second,
        controlled_completion::value,
        child_result::success(4));
    wait_returned(f.second);

    {
        std::lock_guard lock{f.aggregate->mtx};
        EXPECT_FALSE(f.aggregate->done);
        EXPECT_EQ(f.aggregate->completions, 0);
    }
    EXPECT_FALSE(destroyed);

    release_stop_callback(f.second);
    wait_done(f.aggregate);
    wait_returned(f.first);

    EXPECT_TRUE(destroyed);
    EXPECT_FALSE(context.has_value);
    std::lock_guard lock{f.aggregate->mtx};
    ASSERT_EQ(f.aggregate->completions, 1);
    ASSERT_TRUE(f.aggregate->value.has_value());
    auto [error, payload] = *f.aggregate->value;
    EXPECT_EQ(error, std::make_error_code(std::errc::connection_reset));
    ASSERT_TRUE(payload.first.has_value());
    ASSERT_TRUE(payload.second.has_value());
}

TEST(ForgeCoroCombinatorsTest, WhenAllResultsRaceErrorAndValueCompletesOnce) {
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

TEST(ForgeCoroCombinatorsTest, CoroutinesNotAvailable) {
    GTEST_SKIP() << "C++20 coroutines not available on this toolchain";
}

#endif
