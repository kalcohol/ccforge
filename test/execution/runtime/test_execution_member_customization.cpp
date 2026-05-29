#include <gtest/gtest.h>
#include <execution>
#include <exception>
#include <tuple>
#include <type_traits>
#include <utility>

namespace {

struct member_receiver {
    using receiver_concept = std::execution::receiver_t;

    int* value = nullptr;
    bool* errored = nullptr;
    bool* stopped = nullptr;

    void set_value(int v) && noexcept {
        *value = v;
    }

    void set_error(std::exception_ptr) && noexcept {
        *errored = true;
    }

    void set_stopped() && noexcept {
        *stopped = true;
    }

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }
};

struct member_sender {
    using sender_concept = std::execution::sender_t;
    using completions = std::execution::completion_signatures<
        std::execution::set_value_t(int),
        std::execution::set_error_t(std::exception_ptr),
        std::execution::set_stopped_t()>;

    int value;

    template<class Self, class Env>
    static constexpr auto get_completion_signatures() noexcept -> completions {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }

    template<class R>
    struct op : std::execution::__forge_detail::__immovable {
        using operation_state_concept = std::execution::operation_state_t;

        R rcvr;
        int value;

        op(R r, int v) noexcept(std::is_nothrow_move_constructible_v<R>)
            : rcvr(std::move(r)), value(v) {}

        void start() & noexcept {
            std::execution::set_value(std::move(rcvr), value);
        }
    };

    template<std::execution::receiver R>
    auto connect(R r) && noexcept(std::is_nothrow_move_constructible_v<R>) -> op<R> {
        return op<R>{std::move(r), value};
    }

    template<std::execution::receiver R>
    auto connect(R r) const& noexcept(std::is_nothrow_move_constructible_v<R>) -> op<R> {
        return op<R>{std::move(r), value};
    }
};

struct member_scheduler {
    using scheduler_concept = std::execution::scheduler_t;

    auto schedule() const noexcept -> member_sender {
        return member_sender{21};
    }

    bool operator==(const member_scheduler&) const noexcept = default;
};

static_assert(std::execution::receiver<member_receiver>);
static_assert(std::execution::receiver_of<member_receiver, member_sender::completions>);
static_assert(std::execution::sender<member_sender>);
static_assert(std::execution::sender_in<member_sender, std::execution::empty_env>);
static_assert(std::execution::sender_to<member_sender, member_receiver>);

using member_cs_t = std::execution::completion_signatures_of_t<
    member_sender, std::execution::empty_env>;
static_assert(std::is_same_v<member_cs_t, member_sender::completions>);
using member_envless_cs_t = decltype(std::execution::get_completion_signatures(
    std::declval<member_sender>()));
static_assert(std::is_same_v<member_envless_cs_t, member_sender::completions>);
static_assert(std::execution::scheduler<member_scheduler>);

} // namespace

TEST(MemberCustomizationTest, ReceiverCompletionMembersDispatch) {
    int value = 0;
    bool errored = false;
    bool stopped = false;

    std::execution::set_value(member_receiver{&value, &errored, &stopped}, 42);
    std::execution::set_error(
        member_receiver{&value, &errored, &stopped}, std::exception_ptr{});
    std::execution::set_stopped(member_receiver{&value, &errored, &stopped});

    EXPECT_EQ(value, 42);
    EXPECT_TRUE(errored);
    EXPECT_TRUE(stopped);
}

TEST(MemberCustomizationTest, SenderConnectMemberRuns) {
    int value = 0;
    bool errored = false;
    bool stopped = false;

    auto op = std::execution::connect(
        member_sender{7}, member_receiver{&value, &errored, &stopped});
    static_assert(std::execution::operation_state<decltype(op)>);

    std::execution::start(op);

    EXPECT_EQ(value, 7);
    EXPECT_FALSE(errored);
    EXPECT_FALSE(stopped);
}

TEST(MemberCustomizationTest, SenderWorksThroughSyncWait) {
    auto result = std::execution::sync_wait(member_sender{13});

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 13);
}

TEST(MemberCustomizationTest, SchedulerScheduleMemberRuns) {
    member_scheduler sch;
    auto result = std::execution::sync_wait(std::execution::schedule(sch));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 21);
}
