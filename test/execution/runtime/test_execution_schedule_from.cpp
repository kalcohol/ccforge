#include <gtest/gtest.h>

#include <execution>

#include <exception>
#include <functional>
#include <memory>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>

namespace {

struct literal_sender {
    using sender_concept = std::execution::sender_t;
    using completion_signatures = std::execution::completion_signatures<
        std::execution::set_value_t(int)>;
    int value;
};

static_assert([] {
    auto marker = std::execution::schedule_from(literal_sender{42});
    auto& [tag, data, child] = marker;
    const auto& [const_tag, const_data, const_child] = std::as_const(marker);
    auto&& [move_tag, move_data, move_child] = std::move(marker);
    const auto&& [const_move_tag, const_move_data, const_move_child] = std::move(marker);
    return child.value == 42 && const_child.value == 42 &&
        move_child.value == 42 && const_move_child.value == 42;
}());

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

static_assert(std::tuple_size_v<marked_multi_sender_t> == 3);
static_assert(std::tuple_size_v<const marked_multi_sender_t> == 3);
static_assert(std::tuple_size_v<volatile marked_multi_sender_t> == 3);
static_assert(std::tuple_size_v<const volatile marked_multi_sender_t> == 3);
static_assert(std::same_as<
    std::tuple_element_t<0, marked_multi_sender_t>,
    std::execution::schedule_from_t>);
static_assert(std::same_as<
    std::tuple_element_t<2, marked_multi_sender_t>,
    decltype(std::execution::just(1, 2))>);

using move_only_child_t = decltype(std::execution::just(std::unique_ptr<int>{}));
using move_only_marker_t = decltype(std::execution::schedule_from(
    std::declval<move_only_child_t>()));
using marker_data_t = std::tuple_element_t<1, move_only_marker_t>;

static_assert(!std::copy_constructible<move_only_marker_t>);
static_assert(std::same_as<
    std::tuple_element_t<2, move_only_marker_t>, move_only_child_t>);
static_assert(std::same_as<
    std::tuple_element_t<1, const move_only_marker_t>, const marker_data_t>);
static_assert(std::same_as<
    std::tuple_element_t<2, const move_only_marker_t>, const move_only_child_t>);
static_assert(std::same_as<
    std::tuple_element_t<1, volatile move_only_marker_t>, volatile marker_data_t>);
static_assert(std::same_as<
    std::tuple_element_t<2, const volatile move_only_marker_t>,
    const volatile move_only_child_t>);

static_assert(std::same_as<
    decltype(std::declval<move_only_marker_t&>().get<1>()), marker_data_t&>);
static_assert(std::same_as<
    decltype(std::declval<const move_only_marker_t&>().get<1>()), const marker_data_t&>);
static_assert(std::same_as<
    decltype(std::declval<move_only_marker_t&&>().get<1>()), marker_data_t&&>);
static_assert(std::same_as<
    decltype(std::declval<const move_only_marker_t&&>().get<1>()), const marker_data_t&&>);
static_assert(std::same_as<
    decltype(std::declval<move_only_marker_t&>().get<2>()), move_only_child_t&>);
static_assert(std::same_as<
    decltype(std::declval<const move_only_marker_t&>().get<2>()), const move_only_child_t&>);
static_assert(std::same_as<
    decltype(std::declval<move_only_marker_t&&>().get<2>()), move_only_child_t&&>);
static_assert(std::same_as<
    decltype(std::declval<const move_only_marker_t&&>().get<2>()), const move_only_child_t&&>);

struct departure_domain {
    inline static bool transformed = false;

    template<class S, class Env>
        requires std::same_as<
            std::execution::tag_of_t<S>,
            std::execution::schedule_from_t>
    auto transform_sender(
        std::execution::set_value_t,
        S&& sndr,
        const Env&) const noexcept {
        [[maybe_unused]] auto&& [tag, data, child] = std::forward<S>(sndr);
        static_assert(std::same_as<
            std::remove_cvref_t<decltype(tag)>, std::execution::schedule_from_t>);
        static_assert(std::execution::sender<std::remove_cvref_t<decltype(child)>>);
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

TEST(ScheduleFromTest, StructuredBindingLvalueAliasesMoveOnlyChild) {
    auto marker = std::execution::schedule_from(
        std::execution::just(std::make_unique<int>(42)));
    auto& [tag, data, child] = marker;

    static_assert(std::same_as<decltype(tag), std::execution::schedule_from_t>);
    static_assert(std::same_as<decltype(data), marker_data_t>);
    static_assert(std::same_as<decltype(child), move_only_child_t>);
    static_assert(std::same_as<decltype((child)), move_only_child_t&>);
    EXPECT_EQ(std::addressof(data), std::addressof(marker.get<1>()));
    EXPECT_EQ(std::addressof(child), std::addressof(marker.get<2>()));

    auto result = std::this_thread::sync_wait(std::move(child));
    ASSERT_TRUE(result.has_value());
    ASSERT_NE(std::get<0>(*result), nullptr);
    EXPECT_EQ(*std::get<0>(*result), 42);
}

TEST(ScheduleFromTest, StructuredBindingConstLvalueDoesNotCopyMoveOnlyChild) {
    auto marker = std::execution::schedule_from(
        std::execution::just(std::make_unique<int>(42)));
    const auto& [tag, data, child] = marker;

    static_assert(std::same_as<decltype(tag), const std::execution::schedule_from_t>);
    static_assert(std::same_as<decltype(data), const marker_data_t>);
    static_assert(std::same_as<decltype(child), const move_only_child_t>);
    static_assert(std::same_as<decltype((child)), const move_only_child_t&>);
    EXPECT_EQ(std::addressof(data), std::addressof(std::as_const(marker).get<1>()));
    EXPECT_EQ(std::addressof(child), std::addressof(std::as_const(marker).get<2>()));

    auto result = std::this_thread::sync_wait(std::move(marker));
    ASSERT_TRUE(result.has_value());
    ASSERT_NE(std::get<0>(*result), nullptr);
    EXPECT_EQ(*std::get<0>(*result), 42);
}

TEST(ScheduleFromTest, StructuredBindingRvalueCanMoveChildWithoutCopying) {
    auto marker = std::execution::schedule_from(
        std::execution::just(std::make_unique<int>(42)));
    auto&& [tag, data, child] = std::move(marker);

    static_assert(std::same_as<decltype(tag), std::execution::schedule_from_t>);
    static_assert(std::same_as<decltype(data), marker_data_t>);
    static_assert(std::same_as<decltype(child), move_only_child_t>);
    static_assert(std::same_as<decltype(std::move(child)), move_only_child_t&&>);
    EXPECT_EQ(std::addressof(data), std::addressof(marker.get<1>()));
    EXPECT_EQ(std::addressof(child), std::addressof(marker.get<2>()));

    auto result = std::this_thread::sync_wait(std::move(child));
    ASSERT_TRUE(result.has_value());
    ASSERT_NE(std::get<0>(*result), nullptr);
    EXPECT_EQ(*std::get<0>(*result), 42);
}

TEST(ScheduleFromTest, StructuredBindingConstRvalueDoesNotMoveChild) {
    auto marker = std::execution::schedule_from(
        std::execution::just(std::make_unique<int>(42)));
    const auto&& [tag, data, child] = std::move(marker);

    static_assert(std::same_as<decltype(tag), const std::execution::schedule_from_t>);
    static_assert(std::same_as<decltype(data), const marker_data_t>);
    static_assert(std::same_as<decltype(child), const move_only_child_t>);
    static_assert(std::same_as<decltype(std::move(child)), const move_only_child_t&&>);
    static_assert(!std::is_constructible_v<move_only_child_t, decltype(std::move(child))>);
    EXPECT_EQ(std::addressof(data), std::addressof(std::as_const(marker).get<1>()));
    EXPECT_EQ(std::addressof(child), std::addressof(std::as_const(marker).get<2>()));

    auto result = std::this_thread::sync_wait(std::move(marker));
    ASSERT_TRUE(result.has_value());
    ASSERT_NE(std::get<0>(*result), nullptr);
    EXPECT_EQ(*std::get<0>(*result), 42);
}

TEST(ScheduleFromTest, StructuredBindingByValueMovesOwningChild) {
    auto marker = std::execution::schedule_from(
        std::execution::just(std::make_unique<int>(42)));
    auto [tag, data, child] = std::move(marker);

    static_assert(std::same_as<decltype(tag), std::execution::schedule_from_t>);
    static_assert(std::same_as<decltype(data), marker_data_t>);
    static_assert(std::same_as<decltype(child), move_only_child_t>);
    EXPECT_NE(std::addressof(child), std::addressof(marker.get<2>()));

    auto result = std::this_thread::sync_wait(std::move(child));
    ASSERT_TRUE(result.has_value());
    ASSERT_NE(std::get<0>(*result), nullptr);
    EXPECT_EQ(*std::get<0>(*result), 42);
}

TEST(ScheduleFromTest, StructuredBindingPreservesReferencePayload) {
    int value = 7;
    auto marker = std::execution::schedule_from(std::execution::just(std::ref(value)));
    const auto& [tag, data, child] = marker;

    static_assert(std::same_as<decltype(tag), const std::execution::schedule_from_t>);
    EXPECT_EQ(std::addressof(data), std::addressof(std::as_const(marker).get<1>()));
    EXPECT_EQ(std::addressof(child), std::addressof(std::as_const(marker).get<2>()));

    auto result = std::this_thread::sync_wait(std::move(marker));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::addressof(std::get<0>(*result).get()), std::addressof(value));
    std::get<0>(*result).get() = 17;
    EXPECT_EQ(value, 17);
}

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
