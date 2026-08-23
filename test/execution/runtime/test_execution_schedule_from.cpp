#include <gtest/gtest.h>

#include <execution>

#include <exception>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>

namespace {

using marked_multi_sender_t = decltype(std::execution::schedule_from(
    std::execution::just(1, 2)));
using marked_multi_signatures_t = std::execution::completion_signatures_of_t<
    marked_multi_sender_t,
    std::execution::empty_env>;

static_assert(std::same_as<
    std::execution::tag_of_t<marked_multi_sender_t>,
    std::execution::schedule_from_t>);
static_assert(std::same_as<
    marked_multi_signatures_t,
    std::execution::completion_signatures<
        std::execution::set_value_t(int, int)>>);

struct departure_domain {
    inline static bool transformed = false;

    template<class S, class Env>
        requires std::same_as<
            std::execution::tag_of_t<S>,
            std::execution::schedule_from_t>
    auto transform_sender(
        std::execution::set_value_t,
        S&&,
        const Env&) const noexcept {
        transformed = true;
        return std::execution::just(99);
    }
};

struct departure_env {
    template<class Env>
    friend auto tag_invoke(
        std::execution::get_completion_domain_t<>,
        const departure_env&,
        const Env&) noexcept -> departure_domain {
        return {};
    }
};

template<class R>
struct departure_op {
    using operation_state_concept = std::execution::operation_state_t;

    R rcvr;

    void start() & noexcept {
        std::execution::set_value(std::move(rcvr), 1);
    }
};

struct departure_sender {
    using sender_concept = std::execution::sender_t;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<
            std::execution::set_value_t(int)> {
        return {};
    }

    auto get_env() const noexcept -> departure_env { return {}; }

    template<std::execution::receiver R>
    auto connect(R rcvr) && -> departure_op<R> {
        return {std::move(rcvr)};
    }
};

} // namespace

TEST(ScheduleFromTest, DefaultDomainForwardsValues) {
    auto result = std::this_thread::sync_wait(
        std::execution::schedule_from(std::execution::just(42)));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 42);
}

TEST(ScheduleFromTest, DefaultDomainForwardsStopped) {
    auto result = std::this_thread::sync_wait(
        std::execution::schedule_from(std::execution::just_stopped()));

    EXPECT_FALSE(result.has_value());
}

TEST(ScheduleFromTest, DefaultDomainForwardsErrors) {
    auto sender = std::execution::schedule_from(
        std::execution::just_error(std::make_exception_ptr(
            std::runtime_error("schedule_from"))));

    EXPECT_THROW(
        static_cast<void>(std::this_thread::sync_wait(std::move(sender))),
        std::runtime_error);
}

TEST(ScheduleFromTest, ForwardsChildCompletionSchedulerAttribute) {
    std::execution::inline_scheduler scheduler;
    auto sender = std::execution::schedule_from(
        std::execution::schedule(scheduler));
    auto attrs = std::execution::get_env(sender);

    EXPECT_EQ(
        std::execution::get_completion_scheduler<
            std::execution::set_value_t>(attrs),
        scheduler);
}

TEST(ScheduleFromTest, CompletionDomainCanTransformDepartureMarker) {
    departure_domain::transformed = false;
    auto result = std::this_thread::sync_wait(
        std::execution::schedule_from(departure_sender{}));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 99);
    EXPECT_TRUE(departure_domain::transformed);
}

TEST(ScheduleFromTest, ContinuesOnExposesDepartureMarkerToSourceDomain) {
    departure_domain::transformed = false;
    auto result = std::this_thread::sync_wait(
        std::execution::continues_on(
            departure_sender{}, std::execution::inline_scheduler{}));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 99);
    EXPECT_TRUE(departure_domain::transformed);
}
