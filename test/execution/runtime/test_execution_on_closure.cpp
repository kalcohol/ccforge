#include <gtest/gtest.h>

#include <execution>
#include <array>
#include <concepts>
#include <exception>
#include <future>
#include <memory>
#include <thread>
#include <tuple>
#include <utility>

namespace {

namespace ex = std::execution;

enum class category { lvalue, const_lvalue, rvalue, const_rvalue };

struct capture_state {
    std::array<int, 4> scheduler_constructions{};
    std::array<int, 4> closure_constructions{};
    int applications = 0;
    int callbacks = 0;
};

struct tracked_scheduler;

struct tracked_scheduler_attrs {
    capture_state* state;

    auto query(ex::get_completion_scheduler_t<ex::set_value_t>) const noexcept
        -> tracked_scheduler;
};

template<class R>
struct immediate_op {
    using operation_state_concept = ex::operation_state_t;
    R receiver;

    explicit immediate_op(R r) : receiver(std::move(r)) {}
    immediate_op(immediate_op&&) = delete;
    immediate_op(const immediate_op&) = delete;

    void start() & noexcept { ex::set_value(std::move(receiver)); }
};

struct tracked_schedule_sender {
    using sender_concept = ex::sender_t;
    capture_state* state;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> ex::completion_signatures<ex::set_value_t()> { return {}; }

    auto get_env() const noexcept -> tracked_scheduler_attrs { return {state}; }

    template<ex::receiver R>
    auto connect(R r) const -> immediate_op<R> { return immediate_op<R>{std::move(r)}; }
};

struct tracked_scheduler {
    using scheduler_concept = ex::scheduler_t;
    capture_state* state;

    explicit tracked_scheduler(capture_state& s) noexcept : state(&s) {}
    tracked_scheduler(tracked_scheduler& other) noexcept : state(other.state) {
        ++state->scheduler_constructions[0];
    }
    tracked_scheduler(const tracked_scheduler& other) noexcept : state(other.state) {
        ++state->scheduler_constructions[1];
    }
    tracked_scheduler(tracked_scheduler&& other) noexcept : state(other.state) {
        ++state->scheduler_constructions[2];
    }
    tracked_scheduler(const tracked_scheduler&& other) noexcept : state(other.state) {
        ++state->scheduler_constructions[3];
    }

    auto schedule() const noexcept -> tracked_schedule_sender { return {state}; }
    bool operator==(const tracked_scheduler&) const noexcept = default;
};

auto tracked_scheduler_attrs::query(
    ex::get_completion_scheduler_t<ex::set_value_t>) const noexcept -> tracked_scheduler {
    return tracked_scheduler{*state};
}

static_assert(ex::scheduler<tracked_scheduler>);

struct tracked_adaptor {
    capture_state* state;

    explicit tracked_adaptor(capture_state& s) noexcept : state(&s) {}
    tracked_adaptor(tracked_adaptor& other) noexcept : state(other.state) {
        ++state->closure_constructions[0];
    }
    tracked_adaptor(const tracked_adaptor& other) noexcept : state(other.state) {
        ++state->closure_constructions[1];
    }
    tracked_adaptor(tracked_adaptor&& other) noexcept : state(other.state) {
        ++state->closure_constructions[2];
    }
    tracked_adaptor(const tracked_adaptor&& other) noexcept : state(other.state) {
        ++state->closure_constructions[3];
    }

    template<ex::sender S>
    auto operator()(S&& source) && {
        ++state->applications;
        return ex::then(static_cast<S&&>(source), [s = state]() noexcept {
            ++s->callbacks;
            return 42;
        });
    }

    template<ex::sender S>
    friend auto operator|(S&& source, tracked_adaptor&& self) {
        return std::move(self)(static_cast<S&&>(source));
    }
};

template<bool Pipe, class C, class S>
auto apply(C&& closure, S&& source) {
    if constexpr (Pipe) {
        return static_cast<S&&>(source) | static_cast<C&&>(closure);
    } else {
        return static_cast<C&&>(closure)(static_cast<S&&>(source));
    }
}

template<category Category, bool Pipe, class C, class S>
auto apply_category(C& closure, S&& source) {
    if constexpr (Category == category::lvalue) {
        return apply<Pipe>(closure, static_cast<S&&>(source));
    } else if constexpr (Category == category::const_lvalue) {
        return apply<Pipe>(std::as_const(closure), static_cast<S&&>(source));
    } else if constexpr (Category == category::rvalue) {
        return apply<Pipe>(std::move(closure), static_cast<S&&>(source));
    } else {
        return apply<Pipe>(std::move(std::as_const(closure)), static_cast<S&&>(source));
    }
}

template<category Category, bool Pipe>
void check_category() {
    capture_state state;
    auto closure = ex::on(tracked_scheduler{state}, tracked_adaptor{state});
    state.scheduler_constructions = {};
    state.closure_constructions = {};
    auto sender = apply_category<Category, Pipe>(
        closure, ex::schedule(ex::inline_scheduler{}));

    // Isolate application from the later moves needed to construct operation states.
    for (std::size_t i = 0; i != state.scheduler_constructions.size(); ++i) {
        const int expected = i == static_cast<std::size_t>(Category) ? 1 : 0;
        EXPECT_EQ(state.scheduler_constructions[i], expected);
        EXPECT_EQ(state.closure_constructions[i], expected);
    }
    EXPECT_EQ(state.applications, 0);
    EXPECT_EQ(state.callbacks, 0);
    EXPECT_EQ(ex::get_completion_scheduler<ex::set_value_t>(ex::get_env(sender)),
        ex::inline_scheduler{});

    using cs_t = ex::completion_signatures_of_t<decltype(sender), ex::empty_env>;
    static_assert(std::same_as<cs_t, ex::completion_signatures<
        ex::set_value_t(int), ex::set_error_t(std::exception_ptr)>>);
    auto result = ex::sync_wait(std::move(sender));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 42);
    EXPECT_EQ(state.applications, 1);
    EXPECT_EQ(state.callbacks, 1);
}

template<class S>
void expect_value(S sender, int expected = 42) {
    auto result = ex::sync_wait(std::move(sender));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), expected);
}

struct value_adaptor {
    int value = 42;

    template<ex::sender S>
    auto operator()(S&& source) && {
        return ex::then(static_cast<S&&>(source), [v = value]() noexcept { return v; });
    }

    template<ex::sender S, class Self>
        requires std::derived_from<std::remove_cvref_t<Self>, value_adaptor> &&
                 requires(Self&& self, S&& source) {
                     static_cast<Self&&>(self)(static_cast<S&&>(source));
                 }
    friend auto operator|(S&& source, Self&& self) {
        return static_cast<Self&&>(self)(static_cast<S&&>(source));
    }
};

struct mutable_copy_adaptor : value_adaptor {
    mutable_copy_adaptor() = default;
    mutable_copy_adaptor(mutable_copy_adaptor&&) noexcept = default;
    mutable_copy_adaptor(mutable_copy_adaptor& other) noexcept
        : value_adaptor{other.value} {}
    mutable_copy_adaptor(const mutable_copy_adaptor&) = delete;
};

struct const_move_adaptor : value_adaptor {
    const_move_adaptor() = default;
    const_move_adaptor(const_move_adaptor&&) noexcept = default;
    const_move_adaptor(const const_move_adaptor&& other) noexcept
        : value_adaptor{other.value} {}
    const_move_adaptor(const const_move_adaptor&) = delete;
};

struct explicit_copy_adaptor : value_adaptor {
    explicit_copy_adaptor() = default;
    explicit_copy_adaptor(explicit_copy_adaptor&&) noexcept = default;
    explicit explicit_copy_adaptor(const explicit_copy_adaptor& other) noexcept
        : value_adaptor{other.value} {}
};

struct capture_failure {};

struct throwing_move_adaptor : value_adaptor {
    bool* should_throw;

    explicit throwing_move_adaptor(bool& flag) noexcept : should_throw(&flag) {}
    throwing_move_adaptor(const throwing_move_adaptor&) noexcept = default;
    throwing_move_adaptor(throwing_move_adaptor&& other) noexcept(false)
        : value_adaptor{other.value}, should_throw(other.should_throw) {
        if (*should_throw) {
            throw capture_failure{};
        }
    }
};

struct loop_worker {
    ex::run_loop& loop;
    std::promise<std::thread::id> ready;
    std::thread worker;

    explicit loop_worker(ex::run_loop& l)
        : loop(l), worker([this] {
            ready.set_value(std::this_thread::get_id());
            loop.run();
        }) {}

    ~loop_worker() {
        loop.finish();
        worker.join();
    }
};

} // namespace

TEST(ExecutionOnClosure, ForwardsBothCapturesAcrossAllCvrefCategories) {
    check_category<category::lvalue, false>();
    check_category<category::const_lvalue, false>();
    check_category<category::rvalue, false>();
    check_category<category::const_rvalue, false>();
    check_category<category::lvalue, true>();
    check_category<category::const_lvalue, true>();
    check_category<category::rvalue, true>();
    check_category<category::const_rvalue, true>();
}

TEST(ExecutionOnClosure, MoveOnlyStandardClosuresSupportCallAndPipe) {
    auto make_closure = [] {
        return ex::on(ex::inline_scheduler{},
            ex::then([value = std::make_unique<int>(42)]() noexcept { return *value; }));
    };
    auto first = make_closure();
    expect_value(std::move(first)(ex::schedule(ex::inline_scheduler{})));
    auto second = make_closure();
    expect_value(ex::schedule(ex::inline_scheduler{}) | std::move(second));
}

TEST(ExecutionOnClosure, MutableCopyCapturesCanBeAppliedRepeatedly) {
    auto closure = ex::on(ex::inline_scheduler{}, mutable_copy_adaptor{});
    expect_value(closure(ex::schedule(ex::inline_scheduler{})));
    expect_value(ex::schedule(ex::inline_scheduler{}) | closure);
}

TEST(ExecutionOnClosure, ConstRvalueCaptureDoesNotFallBackToConstLvalue) {
    const auto first = ex::on(ex::inline_scheduler{}, const_move_adaptor{});
    expect_value(std::move(first)(ex::schedule(ex::inline_scheduler{})));
    const auto second = ex::on(ex::inline_scheduler{}, const_move_adaptor{});
    expect_value(ex::schedule(ex::inline_scheduler{}) | std::move(second));
}

TEST(ExecutionOnClosure, ApplicationDirectInitializesExplicitCopyCaptures) {
    const auto closure = ex::on(ex::inline_scheduler{}, explicit_copy_adaptor{});
    expect_value(closure(ex::schedule(ex::inline_scheduler{})));
    expect_value(ex::schedule(ex::inline_scheduler{}) | closure);
}

TEST(ExecutionOnClosure, ThrowingMovesPropagateWithoutDisablingValidCopies) {
    bool should_throw = false;
    auto first = ex::on(ex::inline_scheduler{}, throwing_move_adaptor{should_throw});
    auto second = ex::on(ex::inline_scheduler{}, throwing_move_adaptor{should_throw});
    should_throw = true;
    EXPECT_THROW(std::move(first)(ex::schedule(ex::inline_scheduler{})), capture_failure);
    EXPECT_THROW(ex::schedule(ex::inline_scheduler{}) | std::move(second), capture_failure);
    ASSERT_TRUE(should_throw);
    auto copied = first(ex::schedule(ex::inline_scheduler{}));
    auto piped_copy = ex::schedule(ex::inline_scheduler{}) | std::as_const(second);
    // Application copies must succeed while armed; later operation-state moves need not.
    should_throw = false;
    expect_value(std::move(copied));
    expect_value(std::move(piped_copy));
}

TEST(ExecutionOnClosure, RunsOnTargetAndReturnsToChildNotReceiverScheduler) {
    ex::run_loop source_loop;
    ex::run_loop target_loop;
    loop_worker source_worker{source_loop};
    loop_worker target_worker{target_loop};
    const auto source_id = source_worker.ready.get_future().get();
    const auto target_id = target_worker.ready.get_future().get();
    std::thread::id source_observed;
    std::thread::id target_observed;
    std::thread::id returned_observed;

    auto source = ex::schedule(source_loop.get_scheduler()) | ex::then([&]() noexcept {
        source_observed = std::this_thread::get_id();
        return 7;
    });
    auto closure = ex::on(target_loop.get_scheduler(), ex::then([&](int value) noexcept {
        target_observed = std::this_thread::get_id();
        return value + 35;
    }));
    auto sender = std::move(source) | std::move(closure) | ex::then([&](int value) noexcept {
        returned_observed = std::this_thread::get_id();
        return value;
    });
    expect_value(std::move(sender));

    EXPECT_EQ(source_observed, source_id);
    EXPECT_EQ(target_observed, target_id);
    EXPECT_EQ(returned_observed, source_id);
    EXPECT_NE(source_id, target_id);
    EXPECT_NE(returned_observed, std::this_thread::get_id());
}
