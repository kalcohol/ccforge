#include "static_opstate_allocation_probe.hpp"

#if !defined(FORGE_TEST_STATIC_OPSTATE_TSAN)
#include <array>
#include <memory>
#include <stdexcept>

namespace {
namespace ex = std::execution;

enum class disposition { value, error, stopped };

struct schedule_state {
    int connects = 0;
    int starts = 0;
    int live = 0;
    bool fail_connect = false;
    bool aligned = true;
    disposition completion = disposition::value;
};

template<std::size_t Bytes, std::size_t Alignment>
struct test_schedule_sender;

template<std::size_t Bytes = 1, std::size_t Alignment = alignof(std::max_align_t)>
struct test_scheduler {
    using scheduler_concept = ex::scheduler_t;
    schedule_state* state = nullptr;
    auto schedule() const noexcept -> test_schedule_sender<Bytes, Alignment> { return {*this}; }
    bool operator==(const test_scheduler&) const noexcept = default;
};

template<std::size_t Bytes, std::size_t Alignment>
struct test_schedule_sender {
    using sender_concept = ex::sender_t;
    using completion_signatures = ex::completion_signatures<
        ex::set_value_t(), ex::set_error_t(int), ex::set_stopped_t()>;
    test_scheduler<Bytes, Alignment> scheduler;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept -> completion_signatures { return {}; }
    auto get_env() const noexcept {
        return ex::prop(ex::get_completion_scheduler<ex::set_value_t>, scheduler);
    }

    template<class R>
    struct alignas(Alignment) op : ex::__forge_detail::__immovable {
        using operation_state_concept = ex::operation_state_t;
        R receiver;
        schedule_state* state;
        std::array<std::byte, Bytes> padding;

        op(R r, schedule_state* s) : receiver(std::move(r)), state(s) {
            if (state) ++state->live;
        }
        ~op() { if (state) --state->live; }
        void start() & noexcept {
            auto completion = disposition::value;
            if (state) {
                ++state->starts;
                state->aligned &= reinterpret_cast<std::uintptr_t>(this) % Alignment == 0;
                completion = state->completion;
            }
            if (completion == disposition::error) ex::set_error(std::move(receiver), 17);
            else if (completion == disposition::stopped) ex::set_stopped(std::move(receiver));
            else ex::set_value(std::move(receiver));
        }
    };
    template<class R>
    auto connect(R r) const -> op<R> {
        if (scheduler.state) {
            ++scheduler.state->connects;
            if (scheduler.state->fail_connect) throw std::runtime_error("schedule connect");
        }
        return op<R>{std::move(r), scheduler.state};
    }
};

struct result {
    int values = 0;
    int sum = 0;
    int errors = 0;
    int stopped = 0;
};

template<class Token = std::never_stop_token>
struct receiver_env {
    Token token;
    auto query(ex::get_stop_token_t) const noexcept { return token; }
    auto query(ex::get_start_scheduler_t) const noexcept -> ex::inline_scheduler { return {}; }
};

template<class Token = std::never_stop_token>
struct receiver {
    using receiver_concept = ex::receiver_t;
    result* out;
    Token token{};
    template<class... Vs>
    void set_value(Vs... vs) && noexcept {
        ((out->sum += static_cast<int>(vs)), ...);
        ++out->values;
    }
    template<class E>
    void set_error(E&&) && noexcept { ++out->errors; }
    void set_stopped() && noexcept { ++out->stopped; }
    auto get_env() const noexcept { return receiver_env<Token>{token}; }
};

static_assert(sizeof(test_schedule_sender<8192, 16>::op<receiver<>>) > 4096);
static_assert(alignof(test_schedule_sender<1, 64>::op<receiver<>>) >= 64);

template<class S, class Token = std::never_stop_token>
void check(S sender, int sum, std::size_t registrations = 0, Token token = {}) {
    const auto per_registration = allocation_probe::allocations_per_registration();
    result out;
    const auto calls = allocation_probe::measure([&] {
        auto op = ex::connect(std::move(sender), receiver<Token>{&out, token});
        static_assert(noexcept(ex::start(op)));
        ex::start(op);
    });
    EXPECT_EQ(calls, registrations * per_registration);
    EXPECT_EQ(out.values, 1);
    EXPECT_EQ(out.sum, sum);
    EXPECT_EQ(out.errors, 0);
    EXPECT_EQ(out.stopped, 0);
}

struct multi_sender {
    using sender_concept = ex::sender_t;
    int shape;
    using completion_signatures = ex::completion_signatures<
        ex::set_value_t(), ex::set_value_t(int), ex::set_value_t(int, double),
        ex::set_value_t(const int&)>;
    template<class Self, class Env>
    static auto get_completion_signatures() noexcept -> completion_signatures { return {}; }
    auto get_env() const noexcept -> forge::empty_env { return {}; }
    template<class R>
    struct op : ex::__forge_detail::__immovable {
        using operation_state_concept = ex::operation_state_t;
        R recv;
        int shape;
        op(R r, int s) : recv(std::move(r)), shape(s) {}
        void start() & noexcept {
            const int value = 3;
            if (shape == 0) ex::set_value(std::move(recv));
            else if (shape == 1) ex::set_value(std::move(recv), 3);
            else if (shape == 2) ex::set_value(std::move(recv), 3, 4.0);
            else ex::set_value(std::move(recv), value);
        }
    };
    template<class R>
    auto connect(R r) const -> op<R> { return op<R>{std::move(r), shape}; }
};

TEST(ExecutionStaticOpstateTransfer, PositiveControl) {
    EXPECT_EQ(allocation_probe::measure([] {
        void* p = ::operator new(37);
        ::operator delete(p);
    }), 1u);
}

TEST(ExecutionStaticOpstateTransfer, InlineAndLargeAlignedScheduleOperations) {
    check(ex::continues_on(ex::just(42), ex::inline_scheduler{}), 42);
    schedule_state state;
    check(ex::continues_on(ex::just(42), test_scheduler<8192>{&state}), 42);
    check(ex::continues_on(ex::just(42), test_scheduler<1, 64>{&state}), 42);
    EXPECT_EQ(state.connects, 2);
    EXPECT_EQ(state.starts, 2);
    EXPECT_EQ(state.live, 0);
    EXPECT_TRUE(state.aligned);
}

TEST(ExecutionStaticOpstateTransfer, MultipleValueShapesAndDeduplicatedTuples) {
    for (int shape = 0; shape != 4; ++shape) {
        const int expected = shape == 0 ? 0 : shape == 2 ? 7 : 3;
        check(ex::continues_on(multi_sender{shape}, ex::inline_scheduler{}),
            expected);
        check(ex::when_all(ex::continues_on(multi_sender{shape}, ex::inline_scheduler{}),
            ex::just(1)), expected + 1);
        check(ex::continues_on(ex::when_all(multi_sender{shape}, ex::just(1)),
            ex::inline_scheduler{}), expected + 1);
        check(ex::starts_on(ex::inline_scheduler{}, multi_sender{shape}), expected);
        check(ex::on(ex::inline_scheduler{}, multi_sender{shape}), expected);
    }
}

TEST(ExecutionStaticOpstateTransfer, LargeAlignedResultAndNestedCompositions) {
    struct alignas(64) large_result { std::byte bytes[8192]{}; int value = 42; };
    check(ex::continues_on(ex::just(large_result{}), ex::inline_scheduler{})
        | ex::then([](large_result r) noexcept { return r.value; }), 42);
    check(ex::when_all(ex::continues_on(ex::just(1), test_scheduler<8192>{}),
        ex::continues_on(ex::just(2), test_scheduler<1, 64>{})), 3);
    check(ex::let_value(ex::just(1), [](int& v) {
        return ex::continues_on(ex::just(v + 1), test_scheduler<8192, 64>{});
    }), 2);
    check(ex::continues_on(ex::let_value(ex::just(1), [](int& v) {
        return ex::just(v + 1);
    }), test_scheduler<8192>{}), 2);
    check(ex::continues_on(ex::when_all(ex::just(1), ex::just(2)),
        test_scheduler<1, 64>{}), 3);
    // One inner when_all registers on its parent's inplace_stop_token.
    check(ex::when_all(ex::continues_on(ex::when_all(ex::just(1), ex::just(2)),
        ex::inline_scheduler{}), ex::just(3)), 6, 1);
    std::inplace_stop_source source;
    // The top-level when_all registers on the explicitly supplied receiver token.
    check(ex::continues_on(ex::when_all(ex::just(1), ex::just(2)),
        ex::inline_scheduler{}), 3, 1, source.get_token());
}

TEST(ExecutionStaticOpstateTransfer, StartsOnAndBothOnForms) {
    check(ex::starts_on(test_scheduler<8192, 64>{}, ex::just(42)), 42);
    check(ex::starts_on(ex::inline_scheduler{}, ex::schedule(test_scheduler<8192, 64>{}))
        | ex::then([]() noexcept { return 42; }), 42);
    check(ex::on(test_scheduler<8192, 64>{}, ex::just(42)), 42);
    check(ex::on(ex::schedule(test_scheduler<8192, 64>{}), test_scheduler<8192>{},
        ex::then([]() noexcept { return 42; })), 42);
    check(ex::schedule(test_scheduler<8192, 64>{}) |
        ex::on(test_scheduler<1, 64>{}, ex::then([]() noexcept { return 42; })), 42);
}

TEST(ExecutionStaticOpstateTransfer, SyncWaitDrivesRunLoop) {
    auto sender = ex::let_value(ex::read_env(ex::get_scheduler), [](auto& sch) {
        return ex::continues_on(ex::just(42), sch);
    });
    bool completed = false;
    int value = 0;
    const auto calls = allocation_probe::measure([&] {
        auto values = forge::sync_wait(std::move(sender));
        completed = values.has_value();
        if (values) value = std::get<0>(*values);
    });
    EXPECT_EQ(calls, 0u); // No stoppable token registrations in this composition.
    EXPECT_TRUE(completed);
    EXPECT_EQ(value, 42);
}

struct failing_child {
    using sender_concept = ex::sender_t;
    using completion_signatures = ex::completion_signatures<ex::set_value_t(int)>;
    template<class Self, class Env>
    static auto get_completion_signatures() noexcept -> completion_signatures { return {}; }
    template<class R>
    auto connect(R) const -> multi_sender::op<R> {
        throw std::runtime_error("child connect");
    }
    auto get_env() const noexcept -> forge::empty_env { return {}; }
};

TEST(ExecutionStaticOpstateTransfer, EarlyConnectFailuresUnwindConcreteMembers) {
    result out;
    schedule_state state;
    state.fail_connect = true;
    EXPECT_THROW((void)ex::connect(ex::continues_on(ex::just(1), test_scheduler<>{&state}),
        receiver<>{&out}), std::runtime_error);
    EXPECT_EQ(state.live, 0);
    EXPECT_EQ(state.starts, 0);
    state.fail_connect = false;
    EXPECT_THROW((void)ex::connect(ex::continues_on(failing_child{}, test_scheduler<>{&state}),
        receiver<>{&out}), std::runtime_error);
    EXPECT_EQ(state.live, 0);
    EXPECT_THROW((void)ex::connect(ex::starts_on(test_scheduler<>{&state}, failing_child{}),
        receiver<>{&out}), std::runtime_error);
    EXPECT_EQ(state.live, 0);
    EXPECT_THROW((void)ex::connect(ex::on(test_scheduler<>{&state}, failing_child{}),
        receiver<>{&out}), std::runtime_error);
    EXPECT_EQ(state.live, 0);
    EXPECT_EQ(state.starts, 0);
    EXPECT_EQ(out.values + out.errors + out.stopped, 0);
}

struct destroying_receiver : receiver<> {
    void** owner;
    void (**destroy)(void*) noexcept;
    template<class... Vs>
    void set_value(Vs... vs) && noexcept {
        std::move(static_cast<receiver<>&>(*this)).set_value(vs...);
        // Save everything needed before ending this receiver's lifetime.
        auto function = *destroy;
        auto pointer = std::exchange(*owner, nullptr);
        function(pointer);
    }
};

template<class S>
void check_self_destruction(S sender) {
    result out;
    void* owner = nullptr;
    void (*destroy)(void*) noexcept = nullptr;
    using op_t = ex::connect_result_t<S, destroying_receiver>;
    alignas(op_t) std::byte storage[sizeof(op_t)];
    auto* op = ::new (static_cast<void*>(storage)) op_t(ex::connect(std::move(sender),
        destroying_receiver{{&out}, &owner, &destroy}));
    owner = op;
    destroy = [](void* p) noexcept { std::destroy_at(static_cast<op_t*>(p)); };
    ex::start(*op);
    EXPECT_EQ(owner, nullptr);
    EXPECT_EQ(out.values, 1);
    EXPECT_EQ(out.sum, 42);
}

TEST(ExecutionStaticOpstateTransfer, InlineSchedulerAndPredecessorCanDestroyOwner) {
    check_self_destruction(ex::continues_on(ex::just(42), test_scheduler<8192, 64>{}));
    check_self_destruction(ex::starts_on(test_scheduler<8192, 64>{}, ex::just(42)));
    check_self_destruction(ex::on(test_scheduler<8192, 64>{}, ex::just(42)));
    check_self_destruction(ex::on(ex::schedule(test_scheduler<8192, 64>{}),
        test_scheduler<1, 64>{}, ex::then([]() noexcept { return 42; })));
}

TEST(ExecutionStaticOpstateTransfer, SchedulerErrorAndStoppedOverrideSavedValue) {
    for (auto completion : {disposition::error, disposition::stopped}) {
        schedule_state state;
        state.completion = completion;
        result out;
        auto op = ex::connect(ex::continues_on(ex::just(42), test_scheduler<>{&state}),
            receiver<>{&out});
        EXPECT_EQ(state.connects, 1);
        EXPECT_EQ(state.starts, 0);
        ex::start(op);
        EXPECT_EQ(out.values, 0);
        EXPECT_EQ(out.errors, completion == disposition::error ? 1 : 0);
        EXPECT_EQ(out.stopped, completion == disposition::stopped ? 1 : 0);
    }
}
} // namespace
#endif // !defined(FORGE_TEST_STATIC_OPSTATE_TSAN)
