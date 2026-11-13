#include <forge/execution_extensions.hpp>
#include <gtest/gtest.h>
#include <execution>

#include <exception>
#include <initializer_list>
#include <memory>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

namespace {
namespace ex = std::execution;

struct failure { int code; };

struct result {
    int values = 0;
    int errors = 0;
    int stopped = 0;
    std::exception_ptr error;
};

struct receiver {
    using receiver_concept = ex::receiver_t;
    result* out;
    template<class... Vs>
    void set_value(Vs&&...) && noexcept { ++out->values; }
    void set_error(std::exception_ptr e) && noexcept { ++out->errors; out->error = std::move(e); }
    void set_stopped() && noexcept { ++out->stopped; }
    auto get_env() const noexcept -> forge::empty_env { return {}; }
};

struct empty_immovable_sender {
    using sender_concept = ex::sender_t;
    using completion_signatures = ex::completion_signatures<ex::set_value_t()>;
    template<class Self, class Env>
    static auto get_completion_signatures() noexcept -> completion_signatures { return {}; }

    template<class R>
    struct op : ex::__forge_detail::__immovable {
        using operation_state_concept = ex::operation_state_t;
        // These synchronous tests have one operation per receiver type in
        // flight. Keep the receiver outside the operation so the type is empty.
        static inline std::optional<R> recv;

        explicit op(R r) noexcept(std::is_nothrow_move_constructible_v<R>) {
            recv.emplace(std::move(r));
        }
        ~op() { recv.reset(); }
        void start() & noexcept {
            auto r = std::move(*recv);
            recv.reset();
            ex::set_value(std::move(r));
        }
    };

    template<class R>
    auto connect(R r) const noexcept(std::is_nothrow_move_constructible_v<R>) -> op<R> {
        static_assert(std::is_empty_v<op<R>>);
        static_assert(!std::is_final_v<op<R>>);
        static_assert(!std::is_copy_constructible_v<op<R>>);
        static_assert(!std::is_move_constructible_v<op<R>>);
        return op<R>{std::move(r)};
    }
    auto get_env() const noexcept -> forge::empty_env { return {}; }
};

TEST(ExecutionStaticOpstateExceptions, WhenAllEmptyNonFinalImmovableChildrenComplete) {
    result out;
    auto sender = ex::when_all(empty_immovable_sender{}, empty_immovable_sender{}, ex::just());
    auto op = ex::connect(std::move(sender), receiver{&out});
    ex::start(op);
    EXPECT_EQ(out.values, 1);
    EXPECT_EQ(out.errors, 0);
    EXPECT_EQ(out.stopped, 0);
}

TEST(ExecutionStaticOpstateExceptions, LetEmptyNonFinalImmovableOperationsComplete) {
    const auto check = [](auto sender) {
        result out;
        auto op = ex::connect(std::move(sender), receiver{&out});
        ex::start(op);
        EXPECT_EQ(out.values, 1);
        EXPECT_EQ(out.errors, 0);
        EXPECT_EQ(out.stopped, 0);
    };
    // Cover the ordinary predecessor member and each successor variant path.
    check(ex::let_value(empty_immovable_sender{}, [] { return empty_immovable_sender{}; }));
    check(ex::let_error(ex::just_error(1), [](int&) { return empty_immovable_sender{}; }));
    check(ex::let_stopped(ex::just_stopped(), [] { return empty_immovable_sender{}; }));
}

void check_error(const result& out, int code) {
    EXPECT_EQ(out.values, 0);
    EXPECT_EQ(out.errors, 1);
    EXPECT_EQ(out.stopped, 0);
    ASSERT_TRUE(out.error);
    try {
        std::rethrow_exception(out.error);
        FAIL() << "Expected failure";
    } catch (const failure& e) {
        EXPECT_EQ(e.code, code);
    } catch (...) {
        FAIL() << "Unexpected exception type";
    }
}

struct connections {
    int attempts = 0;
    int live = 0;
    int destroyed = 0;
    int starts = 0;
    int throw_on = 0;
};

struct tracked_sender {
    using sender_concept = ex::sender_t;
    using completion_signatures = ex::completion_signatures<ex::set_value_t(int)>;
    template<class Self, class Env>
    static auto get_completion_signatures() noexcept -> completion_signatures { return {}; }
    connections* counts;

    template<class R>
    struct op : ex::__forge_detail::__immovable {
        using operation_state_concept = ex::operation_state_t;
        connections* counts;
        R recv;

        op(connections* c, R r) : counts(c), recv(std::move(r)) { ++counts->live; }
        ~op() { --counts->live; ++counts->destroyed; }
        void start() & noexcept {
            ++counts->starts;
            ex::set_value(std::move(recv), 42);
        }
    };

    template<class R>
    auto connect(R r) const -> op<R> {
        if (++counts->attempts == counts->throw_on) throw failure{17};
        return op<R>{counts, std::move(r)};
    }
    auto get_env() const noexcept -> forge::empty_env { return {}; }
};

TEST(ExecutionStaticOpstateExceptions, WhenAllConnectUnwindsConstructedChildren) {
    // Throw by connection count, independent of the tuple's element
    // construction order. Exercise every partial-construction boundary.
    for (int fail_at = 1; fail_at <= 4; ++fail_at) {
        connections counts;
        counts.throw_on = fail_at;
        result out;
        auto sender = ex::when_all(tracked_sender{&counts}, tracked_sender{&counts},
            tracked_sender{&counts}, tracked_sender{&counts});
        try {
            auto op = ex::connect(std::move(sender), receiver{&out});
            FAIL() << "Expected child connect to throw";
        } catch (const failure& e) {
            EXPECT_EQ(e.code, 17);
        }
        EXPECT_EQ(counts.attempts, fail_at);
        EXPECT_EQ(counts.live, 0);
        EXPECT_EQ(counts.destroyed, fail_at - 1);
        EXPECT_EQ(counts.starts, 0);
        EXPECT_EQ(out.values + out.errors + out.stopped, 0);
    }
}

TEST(ExecutionStaticOpstateExceptions, LetFunctionThrowsOnEachChannel) {
    const auto check = [](auto sender) {
        result out;
        auto op = ex::connect(std::move(sender), receiver{&out});
        ex::start(op);
        check_error(out, 23);
    };
    check(ex::let_value(ex::just(1), [](int&) -> decltype(ex::just()) { throw failure{23}; }));
    check(ex::let_error(ex::just_error(1), [](int&) -> decltype(ex::just()) { throw failure{23}; }));
    check(ex::let_stopped(ex::just_stopped(), []() -> decltype(ex::just()) { throw failure{23}; }));
}

TEST(ExecutionStaticOpstateExceptions, LetSecondConnectThrowsOnEachChannel) {
    const auto check = [](auto make_sender) {
        connections counts;
        counts.throw_on = 1;
        result out;
        {
            auto op = ex::connect(make_sender(counts), receiver{&out});
            ex::start(op);
            check_error(out, 17);
        }
        EXPECT_EQ(counts.live, 0);
        EXPECT_EQ(counts.attempts, 1);
        EXPECT_EQ(counts.starts, 0);
    };
    check([](connections& c) {
        return ex::let_value(ex::just(1), [&c](int&) { return tracked_sender{&c}; });
    });
    check([](connections& c) {
        return ex::let_error(ex::just_error(1), [&c](int&) { return tracked_sender{&c}; });
    });
    check([](connections& c) {
        return ex::let_stopped(ex::just_stopped(), [&c] { return tracked_sender{&c}; });
    });
}

TEST(ExecutionStaticOpstateExceptions, LetOfWhenAllConnectFailureReleasesChildren) {
    connections counts;
    counts.throw_on = 3;
    result out;
    {
        auto sender = ex::let_value(ex::just(), [&] {
            return ex::when_all(tracked_sender{&counts}, tracked_sender{&counts},
                tracked_sender{&counts}, tracked_sender{&counts});
        });
        auto op = ex::connect(std::move(sender), receiver{&out});
        ex::start(op);
        check_error(out, 17);
        EXPECT_EQ(counts.live, 0);
        EXPECT_EQ(counts.destroyed, 2);
        EXPECT_EQ(counts.starts, 0);
    }
    EXPECT_EQ(counts.live, 0);
}

TEST(ExecutionStaticOpstateExceptions, StoredArgumentsReleasedAfterFunctionOrConnectFailure) {
    for (bool fail_in_function : {false, true}) {
        auto owner = std::make_shared<int>(42);
        connections counts;
        counts.throw_on = 1;
        result out;
        {
            auto sender = ex::let_value(ex::just(owner),
                [fail_in_function, &counts](std::shared_ptr<int>&) {
                    if (fail_in_function) throw failure{23};
                    return tracked_sender{&counts};
                });
            auto op = ex::connect(std::move(sender), receiver{&out});
            ex::start(op);
            check_error(out, fail_in_function ? 23 : 17);
            EXPECT_GT(owner.use_count(), 1);
        }
        EXPECT_EQ(owner.use_count(), 1);
        EXPECT_EQ(counts.live, 0);
    }
}

struct choice_sender {
    using sender_concept = ex::sender_t;
    using completion_signatures = ex::completion_signatures<
        ex::set_value_t(int), ex::set_value_t(const int&), ex::set_value_t(double)>;
    template<class Self, class Env>
    static auto get_completion_signatures() noexcept -> completion_signatures { return {}; }
    int choice;

    template<class R>
    struct op : ex::__forge_detail::__immovable {
        using operation_state_concept = ex::operation_state_t;
        int choice;
        R recv;
        op(int c, R r) : choice(c), recv(std::move(r)) {}
        void start() & noexcept {
            if (choice == 0) ex::set_value(std::move(recv), 42);
            else if (choice == 1) {
                const int value = 42;
                ex::set_value(std::move(recv), value);
            } else ex::set_value(std::move(recv), 42.0);
        }
    };
    template<class R>
    auto connect(R r) const -> op<R> { return op<R>{choice, std::move(r)}; }
    auto get_env() const noexcept -> forge::empty_env { return {}; }
};

TEST(ExecutionStaticOpstateExceptions, LetVariantsDeduplicateArgsAndStoreDifferentOperations) {
    for (int choice = 0; choice != 3; ++choice) {
        auto sender = ex::let_value(choice_sender{choice}, []<class T>(T& value) {
            if constexpr (std::is_same_v<T, int>) return ex::just(value);
            else return ex::then(ex::just(static_cast<int>(value)), [](int v) { return v; });
        });
        auto values = forge::sync_wait(std::move(sender));
        ASSERT_TRUE(values);
        EXPECT_EQ(std::get<0>(*values), 42);
    }
}

struct lvalue_sender : tracked_sender {
    template<class R>
    auto connect(R r) & -> tracked_sender::op<R> {
        return tracked_sender::connect(std::move(r));
    }
};

TEST(ExecutionStaticOpstateExceptions, LetPreservesSecondSenderReferenceCategory) {
    connections counts;
    lvalue_sender inner{{&counts}};
    result out;
    {
        auto sender = ex::let_value(ex::just(), [&]() -> lvalue_sender& { return inner; });
        auto op = ex::connect(std::move(sender), receiver{&out});
        ex::start(op);
        EXPECT_EQ(out.values, 1);
        EXPECT_EQ(out.errors, 0);
        EXPECT_EQ(counts.live, 1);
    }
    EXPECT_EQ(counts.live, 0);
    EXPECT_EQ(counts.destroyed, 1);
}

struct argument_counts { int live = 0; int copies = 0; };

struct throwing_argument {
    argument_counts* counts;
    explicit throwing_argument(argument_counts* c) : counts(c) { ++counts->live; }
    throwing_argument(const throwing_argument& other) : counts(other.counts) {
        ++counts->copies;
        throw failure{31};
    }
    ~throwing_argument() { --counts->live; }
};

struct argument_sender {
    using sender_concept = ex::sender_t;
    using completion_signatures = ex::completion_signatures<
        ex::set_value_t(const throwing_argument&)>;
    template<class Self, class Env>
    static auto get_completion_signatures() noexcept -> completion_signatures { return {}; }
    argument_counts* counts;

    template<class R>
    struct op : ex::__forge_detail::__immovable {
        using operation_state_concept = ex::operation_state_t;
        throwing_argument arg;
        R recv;
        op(argument_counts* c, R r) : arg(c), recv(std::move(r)) {}
        void start() & noexcept { ex::set_value(std::move(recv), std::as_const(arg)); }
    };
    template<class R>
    auto connect(R r) const -> op<R> { return op<R>{counts, std::move(r)}; }
    auto get_env() const noexcept -> forge::empty_env { return {}; }
};

TEST(ExecutionStaticOpstateExceptions, LetDecayCopyThrowsAndPredecessorSurvives) {
    argument_counts counts;
    bool invoked = false;
    result out;
    {
        auto sender = ex::let_value(argument_sender{&counts}, [&](throwing_argument&) {
            invoked = true;
            return ex::just();
        });
        auto op = ex::connect(std::move(sender), receiver{&out});
        ex::start(op);
        check_error(out, 31);
        EXPECT_EQ(counts.live, 1);
        EXPECT_EQ(counts.copies, 1);
        EXPECT_FALSE(invoked);
    }
    EXPECT_EQ(counts.live, 0);
}

struct lifetime_state {
    bool predecessor_alive = false;
    bool returned_from_completion = false;
    bool inner_destroyed_while_predecessor_alive = false;
};

struct lifetime_sender {
    using sender_concept = ex::sender_t;
    using completion_signatures = ex::completion_signatures<ex::set_value_t(int)>;
    template<class Self, class Env>
    static auto get_completion_signatures() noexcept -> completion_signatures { return {}; }
    lifetime_state* state;

    template<class R>
    struct op : ex::__forge_detail::__immovable {
        using operation_state_concept = ex::operation_state_t;
        lifetime_state* state;
        R recv;
        op(lifetime_state* s, R r) : state(s), recv(std::move(r)) { state->predecessor_alive = true; }
        ~op() { state->predecessor_alive = false; }
        void start() & noexcept {
            ex::set_value(std::move(recv), 42);
            // A predecessor is allowed to access its state after an inline
            // completion. Replacing it while starting the successor is unsafe.
            state->returned_from_completion = state->predecessor_alive;
        }
    };
    template<class R>
    auto connect(R r) const -> op<R> { return op<R>{state, std::move(r)}; }
    auto get_env() const noexcept -> forge::empty_env { return {}; }
};

struct lifetime_inner_sender {
    using sender_concept = ex::sender_t;
    using completion_signatures = ex::completion_signatures<ex::set_value_t()>;
    template<class Self, class Env>
    static auto get_completion_signatures() noexcept -> completion_signatures { return {}; }
    lifetime_state* state;
    template<class R>
    struct op : ex::__forge_detail::__immovable {
        using operation_state_concept = ex::operation_state_t;
        lifetime_state* state;
        R recv;
        op(lifetime_state* s, R r) : state(s), recv(std::move(r)) {}
        ~op() { state->inner_destroyed_while_predecessor_alive = state->predecessor_alive; }
        void start() & noexcept { ex::set_value(std::move(recv)); }
    };
    template<class R>
    auto connect(R r) const -> op<R> { return op<R>{state, std::move(r)}; }
    auto get_env() const noexcept -> forge::empty_env { return {}; }
};

TEST(ExecutionStaticOpstateExceptions, InlineSuccessKeepsPredecessorThroughInnerDestruction) {
    lifetime_state state;
    result out;
    {
        auto sender = ex::let_value(lifetime_sender{&state}, [&](int&) {
            EXPECT_TRUE(state.predecessor_alive);
            return lifetime_inner_sender{&state};
        });
        auto op = ex::connect(std::move(sender), receiver{&out});
        ex::start(op);
        EXPECT_EQ(out.values, 1);
        EXPECT_TRUE(state.returned_from_completion);
        EXPECT_TRUE(state.predecessor_alive);
    }
    EXPECT_TRUE(state.inner_destroyed_while_predecessor_alive);
    EXPECT_FALSE(state.predecessor_alive);
}
} // namespace
