#include <gtest/gtest.h>
#include <execution>
#include <atomic>
#include <exception>
#include <future>
#include <optional>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <type_traits>

namespace {

struct throwing_query {
    template<class Env>
    int operator()(const Env&) const {
        throw std::runtime_error("read_env query failed");
    }
};

struct throws_on_copy {
    throws_on_copy() = default;
    throws_on_copy(const throws_on_copy&) {
        throw std::runtime_error("optional value construction failed");
    }
};

struct run_loop_workers_guard {
    std::execution::run_loop& first_loop;
    std::execution::run_loop& second_loop;
    std::execution::run_loop& third_loop;
    std::thread& first_worker;
    std::thread& second_worker;
    std::thread& third_worker;

    ~run_loop_workers_guard() {
        first_loop.finish();
        second_loop.finish();
        third_loop.finish();
        if (first_worker.joinable()) {
            first_worker.join();
        }
        if (second_worker.joinable()) {
            second_worker.join();
        }
        if (third_worker.joinable()) {
            third_worker.join();
        }
    }
};

struct throwing_value_sender {
    using sender_concept = std::execution::sender_t;

    template<std::execution::receiver R>
    struct op : std::execution::__forge_detail::__immovable {
        using operation_state_concept = std::execution::operation_state_t;
        R rcvr;
        throws_on_copy value;

        explicit op(R r) : rcvr(std::move(r)) {}

        friend void tag_invoke(std::execution::start_t, op& self) noexcept {
            std::execution::set_value(std::move(self.rcvr), self.value);
        }
    };

    friend auto tag_invoke(std::execution::get_completion_signatures_t,
                           const throwing_value_sender&, auto) noexcept
        -> std::execution::completion_signatures<
            std::execution::set_value_t(const throws_on_copy&)> {
        return {};
    }

    template<std::execution::receiver R>
    friend auto tag_invoke(std::execution::connect_t, throwing_value_sender, R r)
        -> op<R> {
        return op<R>{std::move(r)};
    }

    friend auto tag_invoke(std::execution::get_env_t, const throwing_value_sender&) noexcept
        -> std::execution::empty_env {
        return {};
    }
};

struct stack_value_sender {
    using sender_concept = std::execution::sender_t;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<std::execution::set_value_t(int)> {
        return {};
    }

    template<std::execution::receiver R>
    struct op : std::execution::__forge_detail::__immovable {
        using operation_state_concept = std::execution::operation_state_t;
        R rcvr;

        explicit op(R r) : rcvr(std::move(r)) {}

        void start() & noexcept {
            int value = 41;
            std::execution::set_value(std::move(rcvr), value);
        }
    };

    template<std::execution::receiver R>
    auto connect(R r) const -> op<R> {
        return op<R>{std::move(r)};
    }

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }
};

struct start_scheduler_env {
    std::execution::inline_scheduler scheduler;

    auto query(std::execution::get_start_scheduler_t) const noexcept
        -> std::execution::inline_scheduler {
        return scheduler;
    }
};

struct int_start_receiver {
    using receiver_concept = std::execution::receiver_t;

    int* value = nullptr;
    bool* completed = nullptr;
    start_scheduler_env env{};

    void set_value(int v) && noexcept {
        *value = v;
        *completed = true;
    }

    void set_error(std::exception_ptr) && noexcept {
        *completed = false;
    }

    void set_stopped() && noexcept {
        *completed = false;
    }

    auto get_env() const noexcept -> start_scheduler_env {
        return env;
    }
};

template<class CS>
struct has_exception_ptr_error : std::false_type {};

template<class... Sigs>
struct has_exception_ptr_error<std::execution::completion_signatures<Sigs...>>
    : std::bool_constant<(
          std::same_as<
              Sigs,
              std::execution::set_error_t(std::exception_ptr)> || ...)> {};

} // namespace

TEST(ReadEnvTest, SenderExists) {
    auto sndr = std::execution::read_env(std::execution::get_stop_token);
    static_assert(std::execution::sender<decltype(sndr)>);
    using cs_t = decltype(std::execution::get_completion_signatures(
        sndr, std::execution::empty_env{}));
    static_assert(!has_exception_ptr_error<cs_t>::value);
    SUCCEED();
}

TEST(ReadEnvTest, ThrowingQueryCompletesWithError) {
    auto sndr = std::execution::read_env(throwing_query{});
    using cs_t = decltype(std::execution::get_completion_signatures(
        sndr, std::execution::empty_env{}));
    static_assert(has_exception_ptr_error<cs_t>::value);
    EXPECT_THROW(std::execution::sync_wait(std::move(sndr)), std::runtime_error);
}

TEST(ThenTest, NothrowHandlerDoesNotAdvertiseExceptionPtr) {
    auto sndr = std::execution::just(1)
              | std::execution::then([](int value) noexcept {
                    return value + 1;
                });
    using cs_t = decltype(std::execution::get_completion_signatures(
        sndr, std::execution::empty_env{}));
    static_assert(!has_exception_ptr_error<cs_t>::value);
}

TEST(UponErrorTest, ValuePassThrough) {
    bool fn_called = false;
    auto sndr = std::execution::just(10)
              | std::execution::upon_error([&](auto) { fn_called = true; });
    auto result = std::execution::sync_wait(std::move(sndr));
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(fn_called);
    EXPECT_EQ(std::get<0>(*result), 10);
}

TEST(UponErrorTest, ErrorHandledWithoutThrow) {
    bool fn_called = false;
    auto sndr = std::execution::just_error(42)
              | std::execution::upon_error([&](int) { fn_called = true; });
    auto result = std::execution::sync_wait(std::move(sndr));
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(fn_called);
}

TEST(UponErrorTest, ReportsHandledErrorAsValueSignature) {
    auto sndr = std::execution::just_error(42)
              | std::execution::upon_error([](int) { return 3.14; });
    using cs_t = decltype(std::execution::get_completion_signatures(
        sndr, std::execution::empty_env{}));

    static_assert(std::is_same_v<cs_t,
        std::execution::completion_signatures<
            std::execution::set_value_t(double),
            std::execution::set_error_t(std::exception_ptr)>>);
}

TEST(UponErrorTest, NothrowHandlerDoesNotAdvertiseExceptionPtr) {
    auto sndr = std::execution::just_error(42)
              | std::execution::upon_error([](int) noexcept { return 3.14; });
    using cs_t = decltype(std::execution::get_completion_signatures(
        sndr, std::execution::empty_env{}));
    static_assert(!has_exception_ptr_error<cs_t>::value);
}

TEST(UponStoppedTest, ValuePassThrough) {
    bool fn_called = false;
    auto sndr = std::execution::just(10)
              | std::execution::upon_stopped([&] { fn_called = true; });
    auto result = std::execution::sync_wait(std::move(sndr));
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(fn_called);
    EXPECT_EQ(std::get<0>(*result), 10);
}

TEST(UponStoppedTest, StoppedHandledWithoutThrow) {
    bool fn_called = false;
    auto sndr = std::execution::just_stopped()
              | std::execution::upon_stopped([&] { fn_called = true; });
    auto result = std::execution::sync_wait(std::move(sndr));
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(fn_called);
}

TEST(UponStoppedTest, ReportsHandledStoppedAsValueSignature) {
    auto sndr = std::execution::just_stopped()
              | std::execution::upon_stopped([] { return 7; });
    using cs_t = decltype(std::execution::get_completion_signatures(
        sndr, std::execution::empty_env{}));

    static_assert(std::is_same_v<cs_t,
        std::execution::completion_signatures<
            std::execution::set_value_t(int),
            std::execution::set_error_t(std::exception_ptr)>>);
}

TEST(UponStoppedTest, NothrowHandlerDoesNotAdvertiseExceptionPtr) {
    auto sndr = std::execution::just_stopped()
              | std::execution::upon_stopped([]() noexcept { return 7; });
    using cs_t = decltype(std::execution::get_completion_signatures(
        sndr, std::execution::empty_env{}));
    static_assert(!has_exception_ptr_error<cs_t>::value);
}

TEST(LetValueTest, ChainNewSender) {
    auto sndr = std::execution::just(42)
              | std::execution::let_value([](int x) {
                    return std::execution::just(x + 1);
                });
    auto result = std::execution::sync_wait(std::move(sndr));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 43);
}

TEST(LetValueTest, StoresCompletionValuesForAsyncInnerSender) {
    std::execution::run_loop loop;
    std::thread worker{[&] { loop.run(); }};

    auto sndr = stack_value_sender{}
              | std::execution::let_value([&](int& value) {
                    ++value;
                    return std::execution::schedule(loop.get_scheduler())
                         | std::execution::then([&value] {
                               return value;
                           });
                });

    auto result = std::execution::sync_wait(std::move(sndr));
    loop.finish();
    worker.join();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 42);
}

TEST(LetValueTest, ErrorPassThrough) {
    bool fn_called = false;
    auto sndr = std::execution::just_error(42)
              | std::execution::let_value([&](auto) {
                    fn_called = true;
                    return std::execution::just(0);
                });
    EXPECT_THROW(std::execution::sync_wait(std::move(sndr)), int);
    EXPECT_FALSE(fn_called);
}

TEST(LetValueTest, ReportsInnerSenderSignatures) {
    auto sndr = std::execution::just(42)
              | std::execution::let_value([](int) {
                    return std::execution::just(3.14);
                });
    using cs_t = decltype(std::execution::get_completion_signatures(
        sndr, std::execution::empty_env{}));

    static_assert(std::is_same_v<cs_t,
        std::execution::completion_signatures<
            std::execution::set_value_t(double),
            std::execution::set_error_t(std::exception_ptr)>>);
}

TEST(LetErrorTest, HandleError) {
    bool fn_called = false;
    auto sndr = std::execution::just_error(42)
              | std::execution::let_error([&](int) {
                    fn_called = true;
                    return std::execution::just_stopped();
                });
    auto result = std::execution::sync_wait(std::move(sndr));
    EXPECT_FALSE(result.has_value());
    EXPECT_TRUE(fn_called);
}

TEST(LetErrorTest, ValuePassThrough) {
    bool fn_called = false;
    auto sndr = std::execution::just(10)
              | std::execution::let_error([&](auto) {
                    fn_called = true;
                    return std::execution::just(0);
                });
    auto result = std::execution::sync_wait(std::move(sndr));
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(fn_called);
    EXPECT_EQ(std::get<0>(*result), 10);
}

TEST(LetErrorTest, ReportsInnerSenderSignatures) {
    auto sndr = std::execution::just_error(42)
              | std::execution::let_error([](int) {
                    return std::execution::just_stopped();
                });
    using cs_t = decltype(std::execution::get_completion_signatures(
        sndr, std::execution::empty_env{}));

    static_assert(std::is_same_v<cs_t,
        std::execution::completion_signatures<
            std::execution::set_stopped_t(),
            std::execution::set_error_t(std::exception_ptr)>>);
}

TEST(LetStoppedTest, HandleStopped) {
    bool fn_called = false;
    auto sndr = std::execution::just_stopped()
              | std::execution::let_stopped([&] {
                    fn_called = true;
                    return std::execution::just_stopped();
                });
    auto result = std::execution::sync_wait(std::move(sndr));
    EXPECT_FALSE(result.has_value());
    EXPECT_TRUE(fn_called);
}

TEST(LetStoppedTest, ReportsInnerSenderSignatures) {
    auto sndr = std::execution::just_stopped()
              | std::execution::let_stopped([] {
                    return std::execution::just(7);
                });
    using cs_t = decltype(std::execution::get_completion_signatures(
        sndr, std::execution::empty_env{}));

    static_assert(std::is_same_v<cs_t,
        std::execution::completion_signatures<
            std::execution::set_value_t(int),
            std::execution::set_error_t(std::exception_ptr)>>);
}

TEST(StartsOnTest, RunsOnScheduler) {
    std::execution::run_loop loop;
    auto sch = loop.get_scheduler();

    int result = -1;
    std::thread worker([&] { loop.run(); });

    auto sndr = std::execution::starts_on(sch,
        std::execution::just(42) | std::execution::then([&](int x) {
            result = x;
        }));

    std::execution::sync_wait(std::move(sndr));
    loop.finish();
    worker.join();

    EXPECT_EQ(result, 42);
}

TEST(StartsOnTest, ChildEnvironmentReportsTheTargetScheduler) {
    const std::execution::inline_scheduler target;

    auto scheduler_result = std::execution::sync_wait(
        std::execution::starts_on(
            target,
            std::execution::read_env(std::execution::get_scheduler)));
    auto start_scheduler_result = std::execution::sync_wait(
        std::execution::starts_on(
            target,
            std::execution::read_env(
                std::execution::get_start_scheduler)));

    static_assert(std::is_same_v<
        decltype(scheduler_result),
        std::optional<std::tuple<std::execution::inline_scheduler>>>);
    static_assert(std::is_same_v<
        decltype(start_scheduler_result),
        std::optional<std::tuple<std::execution::inline_scheduler>>>);
    ASSERT_TRUE(scheduler_result.has_value());
    ASSERT_TRUE(start_scheduler_result.has_value());
    EXPECT_EQ(std::get<0>(*scheduler_result), target);
    EXPECT_EQ(std::get<0>(*start_scheduler_result), target);
}

TEST(OnTest, FirstFormReturnsToReceiverStartScheduler) {
    auto sndr = std::execution::on(
        std::execution::inline_scheduler{}, std::execution::just(42));
    using cs_t = decltype(std::execution::get_completion_signatures(
        sndr, start_scheduler_env{}));
    static_assert(std::is_same_v<cs_t,
        std::execution::completion_signatures<
            std::execution::set_value_t(int),
            std::execution::set_error_t(std::exception_ptr)>>);

    int value = 0;
    bool completed = false;
    auto op = std::execution::connect(
        std::move(sndr), int_start_receiver{&value, &completed});
    std::execution::start(op);

    EXPECT_TRUE(completed);
    EXPECT_EQ(value, 42);
}

TEST(OnTest, ClosureFormReturnsToChildCompletionScheduler) {
    auto sndr = std::execution::on(
        std::execution::schedule(std::execution::inline_scheduler{}),
        std::execution::inline_scheduler{},
        std::execution::then([] {
            return 9;
        }));
    using cs_t = decltype(std::execution::get_completion_signatures(
        sndr, std::execution::empty_env{}));
    static_assert(std::is_same_v<cs_t,
        std::execution::completion_signatures<
            std::execution::set_value_t(int),
            std::execution::set_error_t(std::exception_ptr)>>);

    auto result = std::execution::sync_wait(std::move(sndr));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 9);
}

TEST(OnTest, SyncWaitSuppliesTheStartScheduler) {
    auto result = std::execution::sync_wait(
        std::execution::on(
            std::execution::inline_scheduler{},
            std::execution::just(42)));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 42);
}

TEST(OnTest, ClosureFormReturnsToTransferredChildCompletionScheduler) {
    std::execution::run_loop source_loop;
    std::execution::run_loop child_loop;
    std::execution::run_loop closure_loop;
    std::promise<std::thread::id> source_id_promise;
    std::promise<std::thread::id> child_id_promise;
    std::promise<std::thread::id> closure_id_promise;
    auto source_id = source_id_promise.get_future();
    auto child_id = child_id_promise.get_future();
    auto closure_id = closure_id_promise.get_future();

    std::thread source_worker([&] {
        source_id_promise.set_value(std::this_thread::get_id());
        source_loop.run();
    });
    std::thread child_worker([&] {
        child_id_promise.set_value(std::this_thread::get_id());
        child_loop.run();
    });
    std::thread closure_worker([&] {
        closure_id_promise.set_value(std::this_thread::get_id());
        closure_loop.run();
    });
    run_loop_workers_guard workers{
        source_loop, child_loop, closure_loop,
        source_worker, child_worker, closure_worker};

    const auto expected_source_id = source_id.get();
    const auto expected_child_id = child_id.get();
    const auto expected_closure_id = closure_id.get();
    std::thread::id closure_observed;
    std::thread::id returned_observed;

    auto child = std::execution::continues_on(
        std::execution::schedule(source_loop.get_scheduler()),
        child_loop.get_scheduler());
    auto sndr = std::execution::on(
        std::move(child),
        closure_loop.get_scheduler(),
        std::execution::then([&] {
            closure_observed = std::this_thread::get_id();
        }))
        | std::execution::then([&] {
              returned_observed = std::this_thread::get_id();
          });

    auto result = std::execution::sync_wait(std::move(sndr));

    ASSERT_TRUE(result.has_value());
    EXPECT_NE(expected_source_id, expected_child_id);
    EXPECT_EQ(closure_observed, expected_closure_id);
    EXPECT_EQ(returned_observed, expected_child_id);
}

TEST(AffineTest, CompletesOnRequestedScheduler) {
    auto sndr = std::execution::affine(std::execution::just(5));
    using cs_t = decltype(std::execution::get_completion_signatures(
        sndr, start_scheduler_env{}));
    static_assert(std::is_same_v<cs_t,
        std::execution::completion_signatures<
            std::execution::set_value_t(int),
            std::execution::set_error_t(std::exception_ptr)>>);

    auto result = std::execution::sync_wait(std::move(sndr));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 5);
}

TEST(AffineTest, PipeFormCompletesOnRequestedScheduler) {
    auto result = std::execution::sync_wait(
        std::execution::just(6) | std::execution::affine);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 6);
}

TEST(ContinuesOnTest, SupportsStandardPipeForm) {
    auto result = std::execution::sync_wait(
        std::execution::just(7)
        | std::execution::continues_on(std::execution::inline_scheduler{}));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 7);
}

TEST(StoppedAsOptionalTest, SenderExists) {
    auto sndr1 = std::execution::stopped_as_optional(std::execution::just_stopped());
    static_assert(std::execution::sender<decltype(sndr1)>);
    auto sndr2 = std::execution::stopped_as_error(std::execution::just_stopped(), 42);
    static_assert(std::execution::sender<decltype(sndr2)>);
    SUCCEED();
}

TEST(StoppedAsOptionalTest, SupportsPipeForm) {
    auto result = std::execution::sync_wait(
        std::execution::just_stopped() | std::execution::stopped_as_optional);

    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(std::get<0>(*result).has_value());
}

TEST(StoppedAsOptionalTest, WrapsSingleValueInOptional) {
    auto sndr = std::execution::stopped_as_optional(std::execution::just(42));
    using cs_t = decltype(std::execution::get_completion_signatures(
        sndr, std::execution::empty_env{}));
    static_assert(std::is_same_v<cs_t,
        std::execution::completion_signatures<
            std::execution::set_value_t(std::optional<int>)>>);

    auto result = std::execution::sync_wait(std::move(sndr));

    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(std::get<0>(*result).has_value());
    EXPECT_EQ(*std::get<0>(*result), 42);
}

TEST(StoppedAsOptionalTest, WrapsMultiValueInOptionalTuple) {
    auto sndr = std::execution::stopped_as_optional(std::execution::just(1, 2));
    using cs_t = decltype(std::execution::get_completion_signatures(
        sndr, std::execution::empty_env{}));
    static_assert(std::is_same_v<cs_t,
        std::execution::completion_signatures<
            std::execution::set_value_t(std::optional<std::tuple<int, int>>)>>);

    auto result = std::execution::sync_wait(std::move(sndr));

    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(std::get<0>(*result).has_value());
    EXPECT_EQ(std::get<0>(*std::get<0>(*result)), 1);
    EXPECT_EQ(std::get<1>(*std::get<0>(*result)), 2);
}

TEST(StoppedAsOptionalTest, ConvertsStoppedToEmptyOptional) {
    auto sndr = std::execution::stopped_as_optional(std::execution::just_stopped());
    using cs_t = decltype(std::execution::get_completion_signatures(
        sndr, std::execution::empty_env{}));
    static_assert(std::is_same_v<cs_t,
        std::execution::completion_signatures<
            std::execution::set_value_t(std::optional<std::tuple<>>)>>);

    auto result = std::execution::sync_wait(std::move(sndr));

    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(std::get<0>(*result).has_value());
}

TEST(StoppedAsOptionalTest, ValueConstructionThrowCompletesWithError) {
    auto sndr = std::execution::stopped_as_optional(throwing_value_sender{});
    using cs_t = decltype(std::execution::get_completion_signatures(
        sndr, std::execution::empty_env{}));
    static_assert(has_exception_ptr_error<cs_t>::value);

    EXPECT_THROW((void)std::execution::sync_wait(std::move(sndr)), std::runtime_error);
}

TEST(StoppedAsErrorTest, ConvertsStoppedToError) {
    auto sndr = std::execution::stopped_as_error(std::execution::just_stopped(), 42);
    using cs_t = decltype(std::execution::get_completion_signatures(
        sndr, std::execution::empty_env{}));
    static_assert(std::is_same_v<cs_t,
        std::execution::completion_signatures<std::execution::set_error_t(int)>>);

    EXPECT_THROW((void)std::execution::sync_wait(std::move(sndr)), int);
}

TEST(StoppedAsErrorTest, SupportsPipeForm) {
    auto sndr =
        std::execution::just_stopped() | std::execution::stopped_as_error(42);

    EXPECT_THROW((void)std::execution::sync_wait(std::move(sndr)), int);
}
