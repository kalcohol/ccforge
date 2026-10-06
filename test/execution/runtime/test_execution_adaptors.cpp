#include <gtest/gtest.h>
#include <execution>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <future>
#include <initializer_list>
#include <memory>
#include <optional>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>

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

struct starts_on_domain {
    int identity = 0;

    bool operator==(const starts_on_domain&) const noexcept = default;
};

struct domain_scheduler {
    using scheduler_concept = std::execution::scheduler_t;

    int identity = 0;

    auto schedule() const noexcept {
        return std::execution::schedule(std::execution::inline_scheduler{});
    }

    auto query(std::execution::get_domain_t) const noexcept
        -> starts_on_domain {
        return {identity};
    }

    bool operator==(const domain_scheduler&) const noexcept = default;
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

template<class Completion, class Attrs>
concept has_on_completion_scheduler = requires(const Attrs& attrs) {
    std::execution::get_completion_scheduler<Completion>(attrs);
};

struct on_child_domain {};

struct inline_error_sender {
    using sender_concept = std::execution::sender_t;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<std::execution::set_error_t(int)> {
        return {};
    }

    auto get_env() const noexcept {
        return std::execution::make_env(
            std::execution::make_prop(
                std::execution::get_completion_scheduler<std::execution::set_error_t>,
                std::execution::inline_scheduler{}),
            std::execution::make_prop(
                std::execution::get_completion_domain<std::execution::set_error_t>,
                on_child_domain{}));
    }

    template<std::execution::receiver R>
    auto connect(R r) const {
        return std::execution::connect(std::execution::just_error(42), std::move(r));
    }
};

struct borrowed_on_attributes {
    borrowed_on_attributes() = default;
    borrowed_on_attributes(const borrowed_on_attributes&) = delete;

    auto query(std::execution::get_completion_scheduler_t<
               std::execution::set_value_t>) const noexcept
        -> std::execution::inline_scheduler { return {}; }
};

struct borrowed_on_sender : stack_value_sender {
    const borrowed_on_attributes* attrs;
    int* reads;

    borrowed_on_sender(const borrowed_on_attributes& env, int* count) noexcept
        : attrs(&env), reads(count) {}

    auto get_env() const noexcept -> const borrowed_on_attributes& {
        ++*reads;
        return *attrs;
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

enum class continues_hop_completion { value, error, stopped };

struct continues_move_failure {};
struct continues_copy_failure {};
struct continues_connect_failure {};

struct continues_hop_state {
    bool throw_on_move = false;
    bool throw_on_copy = false;
    bool throw_on_connect = false;
    bool inline_hop = false;
    int live_payloads = 0;
    int moves = 0;
    int copies = 0;
    int connects = 0;
    int starts = 0;
    int values = 0;
    int errors = 0;
    int stopped = 0;
    int result = 0;
    int sequence = 0;
    int upstream_order = 0;
    int schedule_order = 0;
    int payload_order = 0;
    int outer_order = 0;
    int abandoned_hops = 0;
    bool payload_alive = false;
    bool outer_alive = false;
    bool upstream_saw_payload = false;
    bool upstream_saw_outer = false;
    bool schedule_saw_payload = false;
    bool schedule_saw_outer = false;
    const void* buffered_payload = nullptr;
    void* source_payload = nullptr;
    const void* outer_env = nullptr;
    const void* delivered_payload = nullptr;
    std::exception_ptr exception;
    void* pending = nullptr;
    void (*resume)(void*, continues_hop_completion) noexcept = nullptr;
    void* self_destroy_op = nullptr;
    void (*destroy)(void*) noexcept = nullptr;

    void complete(continues_hop_completion completion = continues_hop_completion::value) noexcept {
        auto* op = std::exchange(pending, nullptr);
        if (op) {
            resume(op, completion);
        }
    }
};

template<std::size_t Extra = 0, std::size_t Alignment = alignof(void*)>
struct alignas(Alignment) continues_payload {
    continues_hop_state* state;
    int value = 41;
    std::array<std::byte, Extra> padding{};

    explicit continues_payload(continues_hop_state& p) noexcept : state(&p) {
        ++state->live_payloads;
    }

    continues_payload(const continues_payload& other)
        : state(other.state), value(other.value) {
        ++state->copies;
        if (state->throw_on_copy) {
            throw continues_copy_failure{};
        }
        stored();
    }

    continues_payload(continues_payload&& other) noexcept(false)
        : state(other.state), value(other.value) {
        ++state->moves;
        if (state->throw_on_move) {
            throw continues_move_failure{};
        }
        stored();
    }

    void stored() noexcept {
        ++state->live_payloads;
        state->buffered_payload = this;
        state->payload_alive = true;
    }

    ~continues_payload() {
        --state->live_payloads;
        if (state->buffered_payload == this) {
            state->payload_alive = false;
            state->payload_order = ++state->sequence;
        }
    }

    continues_payload* operator&() = delete;
    const continues_payload* operator&() const = delete;
};

static_assert(!std::is_nothrow_move_constructible_v<continues_payload<>>);

struct continues_receiver_env {
    continues_hop_state* state;
};

struct continues_receiver {
    using receiver_concept = std::execution::receiver_t;
    continues_receiver_env env;

    explicit continues_receiver(continues_hop_state& p) noexcept : env{&p} {}
    continues_receiver(const continues_receiver&) = default;
    continues_receiver(continues_receiver&&) = default;

    ~continues_receiver() {
        if (env.state->outer_env == std::addressof(env)) {
            env.state->outer_alive = false;
            env.state->outer_order = ++env.state->sequence;
        }
    }

    void finish() noexcept {
        auto* state = env.state;
        if (state->self_destroy_op) {
            state->destroy(std::exchange(state->self_destroy_op, nullptr));
        }
    }

    template<class T>
    void record(T&& value) noexcept {
        if constexpr (std::same_as<std::remove_cvref_t<T>, int>) {
            env.state->result = value;
        } else {
            env.state->result = value.value;
        }
        env.state->delivered_payload = std::addressof(value);
    }

    template<class T>
    void set_value(T&& value) && noexcept {
        ++env.state->values;
        record(static_cast<T&&>(value));
        finish();
    }

    template<class T>
        requires (!std::same_as<std::remove_cvref_t<T>, std::exception_ptr>)
    void set_error(T&& error) && noexcept {
        ++env.state->errors;
        record(static_cast<T&&>(error));
        finish();
    }

    void set_error(std::exception_ptr error) && noexcept {
        ++env.state->errors;
        env.state->exception = std::move(error);
        finish();
    }

    void set_stopped() && noexcept {
        ++env.state->stopped;
        finish();
    }

    auto get_env() const noexcept -> const continues_receiver_env& { return env; }

    continues_receiver* operator&() = delete;
    const continues_receiver* operator&() const = delete;
};

static_assert(std::execution::receiver<continues_receiver>);

template<bool Error, bool Reference = false, class T = continues_payload<>>
struct continues_source {
    using sender_concept = std::execution::sender_t;
    continues_hop_state* state;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept {
        using arg = std::conditional_t<Reference, const T&, T>;
        if constexpr (Error) {
            return std::execution::completion_signatures<std::execution::set_error_t(arg)>{};
        } else {
            return std::execution::completion_signatures<std::execution::set_value_t(arg)>{};
        }
    }

    template<std::execution::receiver R>
    struct op : std::execution::__forge_detail::__immovable {
        using operation_state_concept = std::execution::operation_state_t;
        continues_hop_state* state;
        R rcvr;
        T value;

        op(continues_hop_state* p, R r) : state(p), rcvr(std::move(r)), value(*p) {
            state->source_payload = std::addressof(value);
        }

        ~op() {
            state->upstream_order = ++state->sequence;
            state->upstream_saw_payload = state->payload_alive;
            state->upstream_saw_outer = state->outer_alive;
            if (state->outer_alive) {
                EXPECT_EQ(std::addressof(std::execution::get_env(rcvr)), state->outer_env);
            }
        }

        void start() & noexcept {
            auto&& arg = [&]() -> decltype(auto) {
                if constexpr (Reference) {
                    return std::as_const(value);
                } else {
                    return std::move(value);
                }
            }();
            if constexpr (Error) {
                std::execution::set_error(std::move(rcvr), static_cast<decltype(arg)&&>(arg));
            } else {
                std::execution::set_value(std::move(rcvr), static_cast<decltype(arg)&&>(arg));
            }
        }
    };

    template<std::execution::receiver R>
    auto connect(R r) const -> op<R> {
        static_assert(std::same_as<std::execution::env_of_t<R>, const continues_receiver_env&>);
        const auto& env = std::execution::get_env(r);
        EXPECT_EQ(env.state, state);
        state->outer_env = std::addressof(env);
        state->outer_alive = true;
        return op<R>{state, std::move(r)};
    }

    auto get_env() const noexcept -> std::execution::empty_env { return {}; }
};

struct continues_schedule_sender;

struct continues_scheduler {
    using scheduler_concept = std::execution::scheduler_t;
    continues_hop_state* state;

    auto schedule() const noexcept -> continues_schedule_sender;
    bool operator==(const continues_scheduler&) const noexcept = default;
};

struct continues_schedule_sender {
    using sender_concept = std::execution::sender_t;
    continues_scheduler scheduler;

    struct attrs {
        continues_scheduler scheduler;

        auto query(std::execution::get_completion_scheduler_t<std::execution::set_value_t>)
            const noexcept -> continues_scheduler { return scheduler; }
    };

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept {
        return std::execution::completion_signatures<
            std::execution::set_value_t(), std::execution::set_error_t(int),
            std::execution::set_stopped_t()>{};
    }

    template<std::execution::receiver R>
    struct op : std::execution::__forge_detail::__immovable {
        using operation_state_concept = std::execution::operation_state_t;
        continues_hop_state* state;
        R rcvr;

        op(continues_hop_state* p, R r) : state(p), rcvr(std::move(r)) {}

        ~op() {
            state->schedule_order = ++state->sequence;
            state->schedule_saw_payload = state->payload_alive;
            state->schedule_saw_outer = state->outer_alive;
            if (state->outer_alive) {
                EXPECT_EQ(std::addressof(std::execution::get_env(rcvr)), state->outer_env);
            }
            if (state->pending == this) {
                state->pending = nullptr;
                ++state->abandoned_hops;
            }
        }

        void start() & noexcept {
            ++state->starts;
            if (state->inline_hop) {
                std::execution::set_value(std::move(rcvr));
                return;
            }
            state->pending = this;
            state->resume = [](void* pointer, continues_hop_completion completion) noexcept {
                auto& self = *static_cast<op*>(pointer);
                if (completion == continues_hop_completion::value) {
                    std::execution::set_value(std::move(self.rcvr));
                } else if (completion == continues_hop_completion::error) {
                    std::execution::set_error(std::move(self.rcvr), 17);
                } else {
                    std::execution::set_stopped(std::move(self.rcvr));
                }
            };
        }
    };

    template<std::execution::receiver R>
    auto connect(R r) const -> op<R> {
        static_assert(std::is_nothrow_move_constructible_v<R>);
        static_assert(std::same_as<std::execution::env_of_t<R>, const continues_receiver_env&>);
        const auto& env = std::execution::get_env(r);
        EXPECT_EQ(env.state, scheduler.state);
        if (scheduler.state->outer_env) {
            EXPECT_EQ(std::addressof(env), scheduler.state->outer_env);
        }
        scheduler.state->outer_env = std::addressof(env);
        scheduler.state->outer_alive = true;
        ++scheduler.state->connects;
        if (scheduler.state->throw_on_connect) {
            throw continues_connect_failure{};
        }
        return op<R>{scheduler.state, std::move(r)};
    }

    auto get_env() const noexcept -> attrs { return {scheduler}; }
};

auto continues_scheduler::schedule() const noexcept -> continues_schedule_sender { return {*this}; }

static_assert(std::execution::scheduler<continues_scheduler>);
static_assert(requires(continues_scheduler& scheduler, const continues_scheduler& const_scheduler) {
    { std::execution::schedule(scheduler) } noexcept -> std::same_as<continues_schedule_sender>;
    { std::execution::schedule(const_scheduler) } noexcept -> std::same_as<continues_schedule_sender>;
    { std::execution::schedule(std::move(scheduler)) } noexcept -> std::same_as<continues_schedule_sender>;
    { std::execution::schedule(std::move(const_scheduler)) } noexcept -> std::same_as<continues_schedule_sender>;
});
static_assert(std::same_as<decltype(std::execution::get_completion_scheduler<
    std::execution::set_value_t>(std::execution::get_env(
        std::declval<const continues_schedule_sender&>()))), continues_scheduler>);

void expect_continues_destruction(const continues_hop_state& state, bool scheduled = true) {
    EXPECT_TRUE(state.upstream_saw_payload);
    EXPECT_TRUE(state.upstream_saw_outer);
    EXPECT_LT(state.upstream_order, state.payload_order);
    if (scheduled) {
        EXPECT_TRUE(state.schedule_saw_payload);
        EXPECT_TRUE(state.schedule_saw_outer);
        EXPECT_LT(state.schedule_order, state.payload_order);
    }
    EXPECT_LT(state.payload_order, state.outer_order);
    EXPECT_FALSE(state.payload_alive);
    EXPECT_FALSE(state.outer_alive);
    EXPECT_EQ(state.live_payloads, 0);
    EXPECT_EQ(state.pending, nullptr);
    EXPECT_EQ(state.abandoned_hops, 0);
}

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

TEST(StartsOnTest, ChildEnvironmentReportsDefaultConstructedTargetDomain) {
    const domain_scheduler target{47};

    auto result = std::execution::sync_wait(
        std::execution::starts_on(
            target,
            std::execution::read_env(std::execution::get_domain)));

    static_assert(std::is_same_v<
        decltype(result),
        std::optional<std::tuple<starts_on_domain>>>);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), starts_on_domain{});
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

TEST(OnTest, FirstFormDoesNotAdvertiseChildValueOrStoppedScheduler) {
    std::execution::run_loop child_loop;
    auto child = std::execution::schedule(child_loop.get_scheduler());
    auto child_attrs = std::execution::get_env(child);
    static_assert(has_on_completion_scheduler<std::execution::set_value_t,
        decltype(child_attrs)>);
    static_assert(has_on_completion_scheduler<std::execution::set_stopped_t,
        decltype(child_attrs)>);

    auto sndr = std::execution::on(std::execution::inline_scheduler{}, std::move(child));
    auto attrs = std::execution::get_env(sndr);
    static_assert(!has_on_completion_scheduler<std::execution::set_value_t,
        decltype(attrs)>);
    static_assert(!has_on_completion_scheduler<std::execution::set_error_t,
        decltype(attrs)>);
    static_assert(!has_on_completion_scheduler<std::execution::set_stopped_t,
        decltype(attrs)>);
    SUCCEED();
}

TEST(OnTest, FirstFormDoesNotAdvertiseChildErrorSchedulerOrDomain) {
    auto child = inline_error_sender{};
    using child_attrs = decltype(std::execution::get_env(child));
    static_assert(has_on_completion_scheduler<std::execution::set_error_t, child_attrs>);
    static_assert(std::is_invocable_v<
        std::execution::get_completion_domain_t<std::execution::set_error_t>,
        child_attrs, std::execution::empty_env>);

    auto sndr = std::execution::on(std::execution::inline_scheduler{}, std::move(child));
    using attrs = decltype(std::execution::get_env(sndr));
    static_assert(!has_on_completion_scheduler<std::execution::set_error_t, attrs>);
    static_assert(!std::is_invocable_v<
        std::execution::get_completion_domain_t<std::execution::set_error_t>,
        attrs, std::execution::empty_env>);
    SUCCEED();
}

TEST(OnTest, FirstFormDoesNotReadOrCopyBorrowedChildAttributes) {
    borrowed_on_attributes child_attrs;
    int reads = 0;
    auto sndr = std::execution::on(
        std::execution::inline_scheduler{}, borrowed_on_sender{child_attrs, &reads});
    auto attrs = std::execution::get_env(sndr);
    static_assert(!has_on_completion_scheduler<std::execution::set_value_t,
        decltype(attrs)>);
    EXPECT_EQ(reads, 0);
}

TEST(OnTest, FirstFormLetInnerEnvironmentMatchesActualReturnScheduler) {
    std::execution::run_loop child_loop;
    std::execution::run_loop target_loop;
    std::execution::run_loop receiver_loop;
    auto child_scheduler = child_loop.get_scheduler();
    auto target_scheduler = target_loop.get_scheduler();
    auto receiver_scheduler = receiver_loop.get_scheduler();

    std::thread child_worker([&] { child_loop.run(); });
    std::thread target_worker([&] { target_loop.run(); });
    std::thread receiver_worker([&] { receiver_loop.run(); });
    run_loop_workers_guard workers{
        child_loop, target_loop, receiver_loop,
        child_worker, target_worker, receiver_worker};
    const auto expected_thread = receiver_worker.get_id();
    std::thread::id observed_thread;

    auto sndr = std::execution::starts_on(
        receiver_scheduler,
        std::execution::let_value(
            std::execution::on(
                target_scheduler, std::execution::schedule(child_scheduler)),
            [&observed_thread]() noexcept {
                observed_thread = std::this_thread::get_id();
                return std::execution::read_env(std::execution::get_start_scheduler);
            }));
    auto result = std::execution::sync_wait(std::move(sndr));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), receiver_scheduler);
    EXPECT_NE(std::get<0>(*result), child_scheduler);
    EXPECT_NE(std::get<0>(*result), target_scheduler);
    EXPECT_EQ(observed_thread, expected_thread);
}

TEST(OnTest, ClosureFormKeepsChildCompletionSchedulerAttribute) {
    std::execution::run_loop child_loop;
    auto child_scheduler = child_loop.get_scheduler();
    auto sndr = std::execution::on(
        std::execution::schedule(child_scheduler),
        std::execution::inline_scheduler{},
        std::execution::then([] {}));
    auto attrs = std::execution::get_env(sndr);

    EXPECT_EQ(std::execution::get_completion_scheduler<std::execution::set_value_t>(attrs),
        child_scheduler);
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

TEST(ContinuesOnTest, ThrowingMoveValueAndErrorSurvivePendingHop) {
    auto check = []<bool Error>() {
        for (bool copy_lvalue : {false, true}) {
            SCOPED_TRACE(Error);
            SCOPED_TRACE(copy_lvalue);
            continues_hop_state state;
            continues_scheduler scheduler{&state};
            EXPECT_EQ(std::execution::get_completion_scheduler<std::execution::set_value_t>(
                std::execution::get_env(std::execution::schedule(scheduler))), scheduler);
            {
                auto sndr = std::execution::continues_on(continues_source<Error>{&state}, scheduler);
                auto run = [&](auto&& source) {
                    auto op = std::execution::connect(std::forward<decltype(source)>(source),
                        continues_receiver{state});
                    std::execution::start(op);
                    EXPECT_NE(state.pending, nullptr);
                    EXPECT_EQ(state.values + state.errors + state.stopped, 0);
                    EXPECT_EQ(state.moves, 1);
                    EXPECT_EQ(state.copies, 0);
                    EXPECT_EQ(state.live_payloads, 2);
                    state.throw_on_move = true;
                    state.complete();
                    EXPECT_EQ(state.values, Error ? 0 : 1);
                    EXPECT_EQ(state.errors, Error ? 1 : 0);
                    EXPECT_EQ(state.stopped, 0);
                    EXPECT_EQ(state.result, 41);
                    EXPECT_FALSE(state.exception);
                    EXPECT_EQ(state.moves, 1);
                    EXPECT_EQ(state.delivered_payload, state.buffered_payload);
                };
                if (copy_lvalue) {
                    run(std::as_const(sndr));
                } else {
                    run(std::move(sndr));
                }
            }
            expect_continues_destruction(state);
        }
    };
    check.template operator()<false>();
    check.template operator()<true>();
}

TEST(ContinuesOnTest, PayloadConstructionFailuresHopBeforeCompletingWithExceptionPtr) {
    auto check = []<bool Error, bool Reference>() {
        SCOPED_TRACE(Error);
        SCOPED_TRACE(Reference);
        continues_hop_state state;
        {
            auto op = std::execution::connect(
                std::execution::continues_on(continues_source<Error, Reference>{&state},
                    continues_scheduler{&state}), continues_receiver{state});
            state.throw_on_move = !Reference;
            state.throw_on_copy = Reference;
            std::execution::start(op);
            EXPECT_EQ(state.values, 0);
            EXPECT_EQ(state.errors, 0);
            EXPECT_EQ(state.stopped, 0);
            EXPECT_EQ(state.connects, 1);
            EXPECT_EQ(state.starts, 1);
            ASSERT_NE(state.pending, nullptr);
            EXPECT_EQ(state.live_payloads, 1);
            EXPECT_FALSE(state.exception);
            state.complete();
            EXPECT_EQ(state.errors, 1);
            EXPECT_EQ(state.pending, nullptr);
            ASSERT_TRUE(state.exception);
            if constexpr (Reference) {
                EXPECT_THROW(std::rethrow_exception(state.exception), continues_copy_failure);
            } else {
                EXPECT_THROW(std::rethrow_exception(state.exception), continues_move_failure);
            }
        }
        EXPECT_TRUE(state.upstream_saw_outer);
        EXPECT_LT(state.upstream_order, state.outer_order);
        EXPECT_EQ(state.live_payloads, 0);
        EXPECT_FALSE(state.payload_alive);
        EXPECT_FALSE(state.outer_alive);
    };
    check.template operator()<false, false>();
    check.template operator()<true, false>();
    check.template operator()<false, true>();
    check.template operator()<true, true>();
}

TEST(ContinuesOnTest, BorrowedValuesAndErrorsAreDecayedBeforeTheHop) {
    auto check = []<bool Error>() {
        SCOPED_TRACE(Error);
        continues_hop_state state;
        {
            auto sndr = std::execution::continues_on(continues_source<Error, true>{&state},
                continues_scheduler{&state});
            using disposition = std::conditional_t<Error,
                std::execution::set_error_t(continues_payload<>),
                std::execution::set_value_t(continues_payload<>)>;
            using cs_t = std::execution::completion_signatures_of_t<decltype(sndr),
                continues_receiver_env>;
            static_assert(std::same_as<cs_t, std::execution::completion_signatures<disposition,
                std::execution::set_error_t(int), std::execution::set_stopped_t(),
                std::execution::set_error_t(std::exception_ptr)>>);
            auto op = std::execution::connect(std::as_const(sndr), continues_receiver{state});
            std::execution::start(op);
            EXPECT_EQ(state.copies, 1);
            EXPECT_EQ(state.moves, 0);
            EXPECT_NE(state.buffered_payload, state.source_payload);
            static_cast<continues_payload<>*>(state.source_payload)->value = 99;
            state.throw_on_move = true;
            state.throw_on_copy = true;
            state.complete();
            EXPECT_EQ(state.values, Error ? 0 : 1);
            EXPECT_EQ(state.errors, Error ? 1 : 0);
            EXPECT_EQ(state.stopped, 0);
            EXPECT_EQ(state.result, 41);
            EXPECT_FALSE(state.exception);
            EXPECT_EQ(state.copies, 1);
            EXPECT_EQ(state.moves, 0);
        }
        expect_continues_destruction(state);
    };
    check.template operator()<false>();
    check.template operator()<true>();
}

TEST(ContinuesOnTest, SchedulerFailureOverridesTheCachedConstructionError) {
    auto check = []<bool Error, bool Reference>() {
        for (auto completion : {continues_hop_completion::error, continues_hop_completion::stopped}) {
            continues_hop_state state;
            auto op = std::execution::connect(
                std::execution::continues_on(continues_source<Error, Reference>{&state},
                    continues_scheduler{&state}), continues_receiver{state});
            state.throw_on_move = !Reference;
            state.throw_on_copy = Reference;
            std::execution::start(op);
            EXPECT_EQ(state.values + state.errors + state.stopped, 0);
            ASSERT_NE(state.pending, nullptr);
            state.complete(completion);
            EXPECT_EQ(state.values, 0);
            EXPECT_EQ(state.errors, completion == continues_hop_completion::error ? 1 : 0);
            EXPECT_EQ(state.stopped, completion == continues_hop_completion::stopped ? 1 : 0);
            EXPECT_FALSE(state.exception);
            if (completion == continues_hop_completion::error) {
                EXPECT_EQ(state.result, 17);
            }
        }
    };
    check.template operator()<false, false>();
    check.template operator()<true, false>();
    check.template operator()<false, true>();
    check.template operator()<true, true>();
}

TEST(ContinuesOnTest, CachedConstructionErrorsAllowInlineCompletionAndSelfDestruction) {
    auto check = []<bool Error, bool Reference>() {
        continues_hop_state state;
        state.inline_hop = true;
        auto sndr = std::execution::continues_on(continues_source<Error, Reference>{&state},
            continues_scheduler{&state});
        using op_t = std::execution::connect_result_t<decltype(sndr), continues_receiver>;
        auto owner = std::unique_ptr<op_t>(new op_t(std::execution::connect(
            std::move(sndr), continues_receiver{state})));
        state.destroy = [](void* op) noexcept { delete static_cast<op_t*>(op); };
        state.self_destroy_op = owner.get();
        state.throw_on_move = !Reference;
        state.throw_on_copy = Reference;
        auto* op = owner.release();
        std::execution::start(*op);
        EXPECT_EQ(state.self_destroy_op, nullptr);
        EXPECT_EQ(state.values, 0);
        EXPECT_EQ(state.errors, 1);
        EXPECT_EQ(state.stopped, 0);
        EXPECT_EQ(state.connects, 1);
        EXPECT_EQ(state.starts, 1);
        EXPECT_EQ(state.pending, nullptr);
        EXPECT_EQ(state.live_payloads, 0);
        EXPECT_EQ(state.abandoned_hops, 0);
        EXPECT_LT(state.upstream_order, state.schedule_order);
        EXPECT_LT(state.schedule_order, state.outer_order);
        ASSERT_TRUE(state.exception);
        if constexpr (Reference) {
            EXPECT_THROW(std::rethrow_exception(state.exception), continues_copy_failure);
        } else {
            EXPECT_THROW(std::rethrow_exception(state.exception), continues_move_failure);
        }
    };
    check.template operator()<false, false>();
    check.template operator()<true, false>();
    check.template operator()<false, true>();
    check.template operator()<true, true>();
}

TEST(ContinuesOnTest, ScheduleConnectFailureKeepsBufferedStateOwned) {
    auto check = []<bool Error>() {
        SCOPED_TRACE(Error);
        continues_hop_state state;
        {
            auto op = std::execution::connect(
                std::execution::continues_on(continues_source<Error>{&state},
                    continues_scheduler{&state}), continues_receiver{state});
            state.throw_on_connect = true;
            std::execution::start(op);
            EXPECT_EQ(state.values, 0);
            EXPECT_EQ(state.errors, 1);
            EXPECT_EQ(state.stopped, 0);
            EXPECT_EQ(state.connects, 1);
            EXPECT_EQ(state.starts, 0);
            EXPECT_EQ(state.pending, nullptr);
            EXPECT_TRUE(state.payload_alive);
            EXPECT_EQ(state.live_payloads, 2);
            ASSERT_TRUE(state.exception);
            EXPECT_THROW(std::rethrow_exception(state.exception), continues_connect_failure);
        }
        EXPECT_EQ(state.schedule_order, 0);
        expect_continues_destruction(state, false);
    };
    check.template operator()<false>();
    check.template operator()<true>();
}

TEST(ContinuesOnTest, PendingAndInlineHopsCanDestroyTheOperationAtCompletion) {
    auto check = []<bool Error>() {
        for (bool inline_hop : {false, true}) {
            SCOPED_TRACE(Error);
            SCOPED_TRACE(inline_hop);
            continues_hop_state state;
            state.inline_hop = inline_hop;
            auto sndr = std::execution::continues_on(continues_source<Error>{&state},
                continues_scheduler{&state});
            using op_t = std::execution::connect_result_t<decltype(sndr), continues_receiver>;
            auto owner = std::unique_ptr<op_t>(new op_t(std::execution::connect(
                std::move(sndr), continues_receiver{state})));
            state.destroy = [](void* op) noexcept { delete static_cast<op_t*>(op); };
            state.self_destroy_op = owner.get();
            auto* op = owner.release();
            std::execution::start(*op);
            if (!inline_hop) {
                EXPECT_NE(state.pending, nullptr);
                EXPECT_EQ(state.values + state.errors + state.stopped, 0);
                state.throw_on_move = true;
                state.complete();
            }
            EXPECT_EQ(state.self_destroy_op, nullptr);
            EXPECT_EQ(state.values, Error ? 0 : 1);
            EXPECT_EQ(state.errors, Error ? 1 : 0);
            EXPECT_EQ(state.stopped, 0);
            EXPECT_EQ(state.result, 41);
            EXPECT_FALSE(state.exception);
            EXPECT_EQ(state.moves, 1);
            expect_continues_destruction(state);
        }
    };
    check.template operator()<false>();
    check.template operator()<true>();
}

TEST(ContinuesOnTest, SchedulerErrorAndStoppedOverrideBufferedCompletion) {
    auto check = []<bool Error>() {
        for (auto completion : {continues_hop_completion::error, continues_hop_completion::stopped}) {
            SCOPED_TRACE(Error);
            SCOPED_TRACE(static_cast<int>(completion));
            continues_hop_state state;
            {
                auto op = std::execution::connect(
                    std::execution::continues_on(continues_source<Error>{&state},
                        continues_scheduler{&state}), continues_receiver{state});
                std::execution::start(op);
                state.throw_on_move = true;
                state.complete(completion);
                EXPECT_EQ(state.values, 0);
                EXPECT_EQ(state.errors, completion == continues_hop_completion::error ? 1 : 0);
                EXPECT_EQ(state.stopped, completion == continues_hop_completion::stopped ? 1 : 0);
                if (completion == continues_hop_completion::error) {
                    EXPECT_EQ(state.result, 17);
                }
                EXPECT_FALSE(state.exception);
                EXPECT_TRUE(state.payload_alive);
                EXPECT_EQ(state.moves, 1);
            }
            expect_continues_destruction(state);
        }
    };
    check.template operator()<false>();
    check.template operator()<true>();
}

TEST(ContinuesOnTest, SchedulerErrorAndStoppedCanDestroyTheOperationAtCompletion) {
    auto check = []<bool Error>() {
        for (auto completion : {continues_hop_completion::error, continues_hop_completion::stopped}) {
            SCOPED_TRACE(Error);
            SCOPED_TRACE(static_cast<int>(completion));
            continues_hop_state state;
            auto sndr = std::execution::continues_on(continues_source<Error>{&state},
                continues_scheduler{&state});
            using op_t = std::execution::connect_result_t<decltype(sndr), continues_receiver>;
            auto owner = std::unique_ptr<op_t>(new op_t(std::execution::connect(
                std::move(sndr), continues_receiver{state})));
            state.destroy = [](void* op) noexcept { delete static_cast<op_t*>(op); };
            state.self_destroy_op = owner.get();
            auto* op = owner.release();
            std::execution::start(*op);
            EXPECT_NE(state.pending, nullptr);
            EXPECT_EQ(state.connects, 1);
            EXPECT_EQ(state.starts, 1);
            EXPECT_EQ(state.values + state.errors + state.stopped, 0);
            EXPECT_EQ(state.live_payloads, 2);
            EXPECT_TRUE(state.payload_alive);
            state.throw_on_move = true;
            state.complete(completion);
            EXPECT_EQ(state.self_destroy_op, nullptr);
            state.complete(completion);
            EXPECT_EQ(state.values, 0);
            EXPECT_EQ(state.errors, completion == continues_hop_completion::error ? 1 : 0);
            EXPECT_EQ(state.stopped, completion == continues_hop_completion::stopped ? 1 : 0);
            if (completion == continues_hop_completion::error) {
                EXPECT_EQ(state.result, 17);
            }
            EXPECT_FALSE(state.exception);
            EXPECT_EQ(state.moves, 1);
            EXPECT_EQ(state.copies, 0);
            expect_continues_destruction(state);
        }
    };
    check.template operator()<false>();
    check.template operator()<true>();
}

TEST(ContinuesOnTest, SetupFailuresCanDestroyTheOperationAtCompletion) {
    auto check = []<bool Error, bool Reference>() {
        for (bool fail_connect : {false, true}) {
            SCOPED_TRACE(Error);
            SCOPED_TRACE(Reference);
            SCOPED_TRACE(fail_connect);
            continues_hop_state state;
            auto sndr = std::execution::continues_on(continues_source<Error, Reference>{&state},
                continues_scheduler{&state});
            using op_t = std::execution::connect_result_t<decltype(sndr), continues_receiver>;
            auto owner = std::unique_ptr<op_t>(new op_t(std::execution::connect(
                std::move(sndr), continues_receiver{state})));
            state.destroy = [](void* op) noexcept { delete static_cast<op_t*>(op); };
            state.self_destroy_op = owner.get();
            state.throw_on_connect = fail_connect;
            state.throw_on_move = !fail_connect && !Reference;
            state.throw_on_copy = !fail_connect && Reference;
            auto* op = owner.release();
            std::execution::start(*op);
            if (!fail_connect) {
                EXPECT_NE(state.self_destroy_op, nullptr);
                EXPECT_EQ(state.values + state.errors + state.stopped, 0);
                ASSERT_NE(state.pending, nullptr);
                state.complete();
            }
            EXPECT_EQ(state.self_destroy_op, nullptr);
            EXPECT_EQ(state.values, 0);
            EXPECT_EQ(state.errors, 1);
            EXPECT_EQ(state.stopped, 0);
            EXPECT_EQ(state.starts, fail_connect ? 0 : 1);
            ASSERT_TRUE(state.exception);
            if (fail_connect) {
                EXPECT_THROW(std::rethrow_exception(state.exception), continues_connect_failure);
                expect_continues_destruction(state, false);
            } else {
                if constexpr (Reference) {
                    EXPECT_THROW(std::rethrow_exception(state.exception), continues_copy_failure);
                } else {
                    EXPECT_THROW(std::rethrow_exception(state.exception), continues_move_failure);
                }
                EXPECT_TRUE(state.upstream_saw_outer);
                EXPECT_LT(state.upstream_order, state.outer_order);
                EXPECT_EQ(state.live_payloads, 0);
                EXPECT_FALSE(state.payload_alive);
                EXPECT_FALSE(state.outer_alive);
                EXPECT_EQ(state.pending, nullptr);
            }
        }
    };
    check.template operator()<false, false>();
    check.template operator()<true, false>();
    check.template operator()<false, true>();
    check.template operator()<true, true>();
}

TEST(ContinuesOnTest, LargeAndOveralignedPayloadsRemainStableAcrossTheHop) {
    auto check = []<bool Error, class T>() {
        SCOPED_TRACE(Error);
        SCOPED_TRACE(sizeof(T));
        SCOPED_TRACE(alignof(T));
        continues_hop_state state;
        {
            auto op = std::execution::connect(
                std::execution::continues_on(continues_source<Error, false, T>{&state},
                    continues_scheduler{&state}), continues_receiver{state});
            std::execution::start(op);
            auto address = reinterpret_cast<std::uintptr_t>(state.buffered_payload);
            auto begin = reinterpret_cast<std::uintptr_t>(std::addressof(op));
            EXPECT_TRUE(address < begin || address >= begin + sizeof(op));
            EXPECT_EQ(address % alignof(T), 0u);
            state.throw_on_move = true;
            state.complete();
            EXPECT_EQ(state.delivered_payload, state.buffered_payload);
            EXPECT_EQ(state.result, 41);
            EXPECT_EQ(state.moves, 1);
            EXPECT_EQ(state.values, Error ? 0 : 1);
            EXPECT_EQ(state.errors, Error ? 1 : 0);
            EXPECT_FALSE(state.exception);
        }
        expect_continues_destruction(state);
    };
    check.template operator()<false, continues_payload<128>>();
    check.template operator()<true, continues_payload<128>>();
    check.template operator()<false, continues_payload<0, 64>>();
    check.template operator()<true, continues_payload<0, 64>>();
}

TEST(ContinuesOnTest, ScalarPayloadUsesInlineStorage) {
    continues_hop_state state;
    {
        auto op = std::execution::connect(std::execution::continues_on(std::execution::just(41),
            continues_scheduler{&state}), continues_receiver{state});
        std::execution::start(op);
        EXPECT_EQ(state.values, 0);
        state.complete();
        EXPECT_EQ(state.values, 1);
        EXPECT_EQ(state.errors + state.stopped, 0);
        EXPECT_EQ(state.result, 41);
        auto address = reinterpret_cast<std::uintptr_t>(state.delivered_payload);
        auto begin = reinterpret_cast<std::uintptr_t>(std::addressof(op));
        EXPECT_GE(address, begin);
        EXPECT_LT(address, begin + sizeof(op));
    }
    EXPECT_TRUE(state.schedule_saw_outer);
    EXPECT_LT(state.schedule_order, state.outer_order);
    EXPECT_EQ(state.pending, nullptr);
    EXPECT_EQ(state.abandoned_hops, 0);
}

TEST(ContinuesOnTest, StoppedHopUsesTheRealAddressOfTheOuterReceiver) {
    continues_hop_state state;
    {
        auto op = std::execution::connect(std::execution::continues_on(std::execution::just_stopped(),
            continues_scheduler{&state}), continues_receiver{state});
        std::execution::start(op);
        EXPECT_EQ(state.stopped, 0);
        state.complete();
        EXPECT_EQ(state.stopped, 1);
        EXPECT_EQ(state.values + state.errors, 0);
    }
    EXPECT_TRUE(state.schedule_saw_outer);
    EXPECT_LT(state.schedule_order, state.outer_order);
    EXPECT_EQ(state.pending, nullptr);
    EXPECT_EQ(state.abandoned_hops, 0);
}

struct optional_stopped_sender {
    using sender_concept = std::execution::sender_t;
    bool stopped = true;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<
            std::execution::set_value_t(int), std::execution::set_stopped_t()> {
        return {};
    }

    template<std::execution::receiver R>
    struct op : std::execution::__forge_detail::__immovable {
        using operation_state_concept = std::execution::operation_state_t;
        R rcvr;
        bool stopped;

        op(R r, bool stop) : rcvr(std::move(r)), stopped(stop) {}

        void start() & noexcept {
            if (stopped) {
                std::execution::set_stopped(std::move(rcvr));
            } else {
                std::execution::set_value(std::move(rcvr), 42);
            }
        }
    };

    template<std::execution::receiver R>
    auto connect(R r) const -> op<R> { return op<R>{std::move(r), stopped}; }

    auto get_env() const noexcept -> std::execution::empty_env { return {}; }
};

TEST(StoppedAsOptionalTest, SenderExists) {
    auto sndr1 = std::execution::stopped_as_optional(optional_stopped_sender{});
    static_assert(std::execution::sender<decltype(sndr1)>);
    auto sndr2 = std::execution::stopped_as_error(std::execution::just_stopped(), 42);
    static_assert(std::execution::sender<decltype(sndr2)>);
    SUCCEED();
}

TEST(StoppedAsOptionalTest, SupportsPipeForm) {
    auto result = std::execution::sync_wait(
        optional_stopped_sender{} | std::execution::stopped_as_optional);

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
    auto sndr = std::execution::stopped_as_optional(optional_stopped_sender{});
    using cs_t = decltype(std::execution::get_completion_signatures(
        sndr, std::execution::empty_env{}));
    static_assert(std::is_same_v<cs_t,
        std::execution::completion_signatures<
            std::execution::set_value_t(std::optional<int>)>>);

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
