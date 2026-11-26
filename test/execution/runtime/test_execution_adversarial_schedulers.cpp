#include "adversarial_schedulers.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <initializer_list>
#include <optional>
#include <thread>
#include <utility>

namespace {

namespace ex = std::execution;
using test_execution::adversarial_schedules;
using test_execution::schedule_kind;

using scheduler = adversarial_schedules::scheduler;
static_assert(ex::scheduler<scheduler>);
static_assert(noexcept(ex::schedule(std::declval<const scheduler&>())));
using schedule_sender = ex::schedule_result_t<scheduler>;
using stoppable_env = ex::prop<std::get_stop_token_t, std::inplace_stop_token>;
static_assert(std::same_as<ex::completion_signatures_of_t<schedule_sender, ex::env<>>,
    ex::completion_signatures<ex::set_value_t()>>);
static_assert(std::same_as<ex::completion_signatures_of_t<const schedule_sender&, stoppable_env>,
    ex::completion_signatures<ex::set_value_t(), ex::set_stopped_t()>>);

constexpr std::array kinds{
    schedule_kind::inline_immediate,
    schedule_kind::never_resume,
    schedule_kind::other_thread,
    schedule_kind::random_delay,
    schedule_kind::stop_at_every_step,
};
constexpr std::uint32_t seed = 0x5eedu;

enum class signal { value, stopped, error, destroyed };
using event = std::pair<int, signal>;

struct log {
    std::array<event, 12> entries{};
    std::size_t size = 0;

    void record(int id, signal what) noexcept {
        if (size < entries.size()) entries[size] = {id, what};
        ++size;
    }

    void expect(std::initializer_list<event> expected) const {
        EXPECT_EQ(size, expected.size());
        EXPECT_TRUE(std::ranges::equal(
            entries.begin(), entries.begin() + std::min(size, entries.size()),
            expected.begin(), expected.end()));
    }
};

struct result {
    std::array<int, 2> values{};
    std::exception_ptr error;
    std::thread::id completion_thread;
};

// Exactly one receiver owns the destruction marker after the connect moves.
struct recording_receiver {
    using receiver_concept = ex::receiver_tag;

    recording_receiver(log& events, result& output, std::inplace_stop_source& source, int id)
        : events(&events), output(&output), source(&source), id(id) {}

    recording_receiver(recording_receiver&& other) noexcept
        : events(std::exchange(other.events, nullptr)), output(other.output),
          source(other.source), id(other.id) {}

    ~recording_receiver() {
        if (events) events->record(id, signal::destroyed);
    }

    auto get_env() const noexcept {
        return ex::prop{std::get_stop_token, source->get_token()};
    }

    void set_value(int first, int second = 0) && noexcept {
        output->values = {first, second};
        complete(signal::value);
    }

    void set_stopped() && noexcept { complete(signal::stopped); }

    void set_error(std::exception_ptr error) && noexcept {
        output->error = std::move(error);
        complete(signal::error);
    }

    void complete(signal which) noexcept {
        output->completion_thread = std::this_thread::get_id();
        events->record(id, which);
    }

    log* events;
    result* output;
    std::inplace_stop_source* source;
    int id;
};

struct sample_error { int code; };

void expect_error(const result& output) {
    ASSERT_TRUE(output.error);
    try {
        std::rethrow_exception(output.error);
    } catch (const sample_error& error) {
        EXPECT_EQ(error.code, 47);
    } catch (...) {
        ADD_FAILURE() << "wrong exception payload";
    }
}

void replay_when_all(schedule_kind kind, signal outcome, bool pre_stop = false,
                     std::optional<std::size_t> stop_at = std::nullopt) {
    SCOPED_TRACE(testing::Message() << "schedule " << static_cast<int>(kind)
        << ", outcome " << static_cast<int>(outcome)
        << ", pre-stop " << pre_stop << ", point " << stop_at.value_or(99));
    std::inplace_stop_source source;
    adversarial_schedules driver{{kind, seed, &source, stop_at}};
    if (pre_stop) source.request_stop();

    log events;
    result output;
    std::array<unsigned, 2> entered{};
    const auto sch = driver.get_scheduler();
    const auto caller = std::this_thread::get_id();
    auto left = ex::schedule(sch) | ex::then([&]() noexcept { ++entered[0]; return 17; });
    auto right = ex::schedule(sch) | ex::then([&] {
        ++entered[1];
        if (outcome == signal::error) throw sample_error{47};
        return 25;
    });

    auto replay = [&](auto&& child) {
        auto op = ex::connect(
            ex::when_all(std::move(left), std::forward<decltype(child)>(child)),
            recording_receiver{events, output, source, 1});
        ex::start(op);
        if (kind == schedule_kind::never_resume) events.expect({});
        if (kind == schedule_kind::inline_immediate || kind == schedule_kind::stop_at_every_step) {
            events.expect({{1, outcome}});
        }
        // Worker modes may complete before or after start() returns. Only
        // observe their log, values and thread ID once callbacks have drained.
        driver.settle();
        EXPECT_EQ(driver.steps(), 2u);
        events.expect({{1, outcome}});
        if (kind == schedule_kind::other_thread || kind == schedule_kind::random_delay) {
            EXPECT_NE(output.completion_thread, caller);
            EXPECT_NE(output.completion_thread, std::thread::id{});
        } else {
            EXPECT_EQ(output.completion_thread, caller);
        }
    };

    if (outcome == signal::error) {
        // The error child must still execute after its sibling stops. The
        // known when_all result in that ordering is error, not stopped.
        replay(ex::unstoppable(std::move(right)));
    } else {
        replay(std::move(right));
    }
    events.expect({{1, outcome}, {1, signal::destroyed}});
    EXPECT_EQ(entered[0], pre_stop || stop_at == 0 ? 0u : 1u);
    EXPECT_EQ(entered[1], outcome == signal::stopped ? 0u : 1u);
    if (outcome == signal::value) EXPECT_EQ(output.values, (std::array{17, 25}));
    if (outcome == signal::error) expect_error(output);
    EXPECT_EQ(source.stop_requested(), pre_stop || stop_at.has_value());
}

// The fixture controls submission of an already-connected run_loop operation.
// It has no stop token itself, so injection reaches the real loop receiver
// and does not short-circuit the submission.
struct loop_submission {
    using receiver_concept = ex::receiver_tag;
    void* operation;
    void (*start_loop_operation)(void*) noexcept;
    const std::inplace_stop_source* source;
    bool* saw_stop;

    void set_value() && noexcept {
        *saw_stop = source->stop_requested();
        start_loop_operation(operation);
    }

    template<class Op>
    static loop_submission for_operation(
        Op& op, const std::inplace_stop_source& source, bool& saw_stop) noexcept {
        return {&op, [](void* p) noexcept { ex::start(*static_cast<Op*>(p)); }, &source, &saw_stop};
    }
};

static_assert(ex::operation_state<ex::connect_result_t<const schedule_sender&, loop_submission>>);

void replay_run_loop(schedule_kind kind, bool pre_stop = false,
                     std::optional<std::size_t> stop_at = std::nullopt) {
    SCOPED_TRACE(testing::Message() << "schedule " << static_cast<int>(kind)
        << ", pre-stop " << pre_stop << ", point " << stop_at.value_or(99));
    std::inplace_stop_source source;
    std::array yield_counts{8u, 8u, 8u};
    adversarial_schedules driver{{kind, seed, &source, stop_at, yield_counts}};
    if (pre_stop) source.request_stop();
    const bool stopped = pre_stop || stop_at.has_value();
    const auto first_signal = stopped ? signal::stopped : signal::value;
    const auto middle_signal = stopped ? signal::stopped : signal::error;

    log events;
    result first_out, middle_out, last_out;
    std::array<bool, 3> submission_saw_stop{};
    {
        ex::run_loop loop;
        const auto sch = loop.get_scheduler();
        auto first = ex::connect(
            ex::schedule(sch) | ex::then([]() noexcept { return 17; }),
            recording_receiver{events, first_out, source, 1});
        auto middle = ex::connect(
            ex::schedule(sch) | ex::then([]() -> int { throw sample_error{47}; }),
            recording_receiver{events, middle_out, source, 2});
        auto last = ex::connect(
            ex::schedule(sch) | ex::then([]() noexcept { return 25; }),
            recording_receiver{events, last_out, source, 3});
        const auto submit = ex::schedule(driver.get_scheduler());
        auto a = ex::connect(submit, loop_submission::for_operation(first, source, submission_saw_stop[0]));
        auto b = ex::connect(submit, loop_submission::for_operation(middle, source, submission_saw_stop[1]));
        auto c = ex::connect(submit, loop_submission::for_operation(last, source, submission_saw_stop[2]));
        ex::start(a);
        ex::start(b);
        ex::start(c);
        driver.settle();
        EXPECT_EQ(driver.steps(), 3u);
        if (kind == schedule_kind::random_delay) {
            EXPECT_EQ(yield_counts, (std::array{4u, 1u, 4u}));
        }
        EXPECT_EQ(source.stop_requested(), stopped);
        for (std::size_t i = 0; i < submission_saw_stop.size(); ++i) {
            EXPECT_EQ(submission_saw_stop[i], pre_stop || (stop_at && i >= *stop_at));
        }
        events.expect({});

        // All submitters have finished and the loop has sole access to its
        // receivers. Even with finish already called, queued work drains FIFO.
        loop.finish();
        loop.run();
        events.expect({{1, first_signal}, {2, middle_signal}, {3, first_signal}});
        EXPECT_EQ(first_out.completion_thread, std::this_thread::get_id());
        EXPECT_EQ(middle_out.completion_thread, std::this_thread::get_id());
        EXPECT_EQ(last_out.completion_thread, std::this_thread::get_id());
    }
    events.expect({{1, first_signal}, {2, middle_signal}, {3, first_signal},
        {3, signal::destroyed}, {2, signal::destroyed}, {1, signal::destroyed}});
    if (!stopped) {
        EXPECT_EQ(first_out.values[0], 17);
        EXPECT_EQ(last_out.values[0], 25);
        expect_error(middle_out);
    } else {
        EXPECT_FALSE(middle_out.error);
    }
}

TEST(ExecutionAdversarialSchedules, SchedulerQueriesMatchExecutionResource) {
    using enum ex::forward_progress_guarantee;
    adversarial_schedules other{{schedule_kind::never_resume}};
    for (auto kind : kinds) {
        SCOPED_TRACE(testing::Message() << "schedule " << static_cast<int>(kind));
        adversarial_schedules driver{{kind}};
        const auto sch = driver.get_scheduler();
        const auto expected = kind == schedule_kind::other_thread || kind == schedule_kind::random_delay
            ? parallel : weakly_parallel;
        EXPECT_EQ(ex::get_forward_progress_guarantee(sch), expected);
        EXPECT_EQ(sch, driver.get_scheduler());
        EXPECT_NE(sch, other.get_scheduler());

        const auto attrs = ex::get_env(ex::schedule(sch));
        EXPECT_EQ(ex::get_completion_scheduler<ex::set_value_t>(attrs), sch);
        EXPECT_EQ(ex::get_completion_scheduler<ex::set_stopped_t>(attrs), sch);
    }
}

TEST(ExecutionAdversarialSchedules, WhenAllKnownOutcomes) {
    for (auto kind : kinds) {
        replay_when_all(kind, signal::value);
        replay_when_all(kind, signal::stopped, true);
        replay_when_all(kind, signal::error);
        replay_when_all(kind, signal::error, true);
    }
}

TEST(ExecutionAdversarialSchedules, RunLoopDrainsInOrder) {
    for (auto kind : kinds) {
        replay_run_loop(kind);
        replay_run_loop(kind, true);
    }
}

TEST(ExecutionAdversarialSchedules, InjectsStopAtEachSubmissionInTurn) {
    for (std::size_t step = 0; step < 2; ++step) {
        replay_when_all(schedule_kind::stop_at_every_step, signal::stopped, false, step);
    }
    replay_when_all(schedule_kind::stop_at_every_step, signal::error, false, 0);
    for (std::size_t step = 0; step < 3; ++step) {
        replay_run_loop(schedule_kind::stop_at_every_step, false, step);
    }
}

TEST(ExecutionAdversarialSchedules, DestroyingUnstartedWorkDoesNotSubmit) {
    std::inplace_stop_source source;
    adversarial_schedules driver{{schedule_kind::never_resume}};
    log events;
    result output;
    {
        auto op = ex::connect(
            ex::schedule(driver.get_scheduler()) | ex::then([]() noexcept { return 17; }),
            recording_receiver{events, output, source, 1});
        (void)op;
        events.expect({});
    }
    driver.settle();
    EXPECT_EQ(driver.steps(), 0u);
    events.expect({{1, signal::destroyed}});
}

TEST(ExecutionAdversarialSchedules, DelayPlanIsBoundedAndSeeded) {
    EXPECT_EQ(adversarial_schedules::delay_at(seed, 0), 4u);
    EXPECT_EQ(adversarial_schedules::delay_at(seed, 1), 1u);
    bool different = false;
    for (std::size_t step = 0; step < 32; ++step) {
        auto delay = adversarial_schedules::delay_at(seed, step);
        EXPECT_LT(delay, 8u);
        different |= delay != adversarial_schedules::delay_at(seed + 1, step);
    }
    EXPECT_TRUE(different);
}

} // namespace
