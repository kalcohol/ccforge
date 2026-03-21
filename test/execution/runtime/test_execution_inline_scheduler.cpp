#include <gtest/gtest.h>

#include <execution>

#include <tuple>
#include <type_traits>
#include <utility>

static_assert(std::execution::scheduler<std::execution::inline_scheduler>);

// ── T-7: scheduler_concept marker is required ────────────────────────────
namespace {

struct no_marker_scheduler {
    no_marker_scheduler() = default;
    bool operator==(const no_marker_scheduler&) const noexcept = default;
    friend auto tag_invoke(std::execution::schedule_t, const no_marker_scheduler&) noexcept {
        return std::execution::schedule(std::execution::inline_scheduler{});
    }
};
static_assert(!std::execution::scheduler<no_marker_scheduler>,
              "scheduler without scheduler_concept marker must be rejected");

} // namespace

TEST(ExecutionInlineSchedulerTest, ScheduleRunsInline) {
    std::execution::inline_scheduler sch{};

    auto result = std::execution::sync_wait(std::execution::schedule(sch));

    ASSERT_TRUE(static_cast<bool>(result));
    using tuple_t = std::remove_cvref_t<decltype(*result)>;
    static_assert(std::tuple_size_v<tuple_t> == 0);
}

TEST(ExecutionInlineSchedulerTest, ScheduleThenChain) {
    std::execution::inline_scheduler sch{};

    auto sender =
        std::execution::schedule(sch) |
        std::execution::then([] { return 123; });

    auto result = std::execution::sync_wait(std::move(sender));

    ASSERT_TRUE(static_cast<bool>(result));
    EXPECT_EQ(std::get<0>(*result), 123);
}

// ── T-2: Scheduler env roundtrip ────────────────────────────────────────

TEST(ExecutionInlineSchedulerTest, ScheduleEnvRoundtrip) {
    std::execution::inline_scheduler sch{};
    auto sndr = std::execution::schedule(sch);
    auto env = std::execution::get_env(sndr);
    auto sch2 = std::execution::get_scheduler(env);
    EXPECT_EQ(sch, sch2);
}
