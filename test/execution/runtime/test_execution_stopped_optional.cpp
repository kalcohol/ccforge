#include <gtest/gtest.h>

#include <execution>
#include <exception>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

namespace {

namespace ex = std::execution;

enum class completion { value, error, stopped };

struct optional_env { using optional_values = void; };

struct empty_void_query {
    void operator()(ex::empty_env) const noexcept {}
    int operator()(optional_env) const noexcept { return 42; }
};

template<bool Multi = false, bool Dependent = false>
struct typed_sender {
    using sender_concept = ex::sender_t;
    completion outcome = completion::stopped;
    using signatures = std::conditional_t<Multi,
        ex::completion_signatures<ex::set_value_t(int, int), ex::set_error_t(int), ex::set_stopped_t()>,
        ex::completion_signatures<ex::set_value_t(int), ex::set_error_t(int), ex::set_stopped_t()>>;

    template<class Self, class Env>
        requires (!Dependent || requires { typename Env::optional_values; })
    static auto get_completion_signatures() noexcept -> signatures { return {}; }

    auto get_env() const noexcept -> ex::empty_env { return {}; }

    template<ex::receiver R>
    struct operation : ex::__forge_detail::__immovable {
        using operation_state_concept = ex::operation_state_t;
        R receiver;
        completion outcome;

        operation(R r, completion c) : receiver(std::move(r)), outcome(c) {}

        void start() & noexcept {
            if (outcome == completion::stopped) {
                ex::set_stopped(std::move(receiver));
            } else if (outcome == completion::error) {
                ex::set_error(std::move(receiver), 7);
            } else if constexpr (Multi) {
                ex::set_value(std::move(receiver), 42, 43);
            } else {
                ex::set_value(std::move(receiver), 42);
            }
        }
    };

    template<ex::receiver R>
    auto connect(R r) const -> operation<R> {
        return operation<R>{std::move(r), outcome};
    }
};

struct observed_result {
    int values = 0;
    int errors = 0;
    int value = 0;
    bool engaged = false;
};

struct optional_receiver {
    using receiver_concept = ex::receiver_t;
    observed_result* result;

    void set_value(std::optional<int>&& value) && noexcept {
        ++result->values;
        result->engaged = value.has_value();
        if (value) {
            result->value = *value;
        }
    }

    void set_error(int value) && noexcept {
        ++result->errors;
        result->value = value;
    }

    auto get_env() const noexcept -> optional_env { return {}; }
};

using scalar_optional = decltype(ex::stopped_as_optional(typed_sender<>{}));
static_assert(std::is_same_v<ex::completion_signatures_of_t<scalar_optional>,
    ex::completion_signatures<ex::set_value_t(std::optional<int>), ex::set_error_t(int)>>);
static_assert(!ex::sends_stopped<scalar_optional>);
static_assert(ex::receiver_of<optional_receiver, ex::completion_signatures_of_t<scalar_optional>>);

struct copy_failure {};

struct throwing_copy {
    int value;
    bool throws;

    throwing_copy(int v, bool t) noexcept : value(v), throws(t) {}
    throwing_copy(const throwing_copy& other) : value(other.value), throws(other.throws) {
        if (throws) {
            throw copy_failure{};
        }
    }
};

struct reference_sender {
    using sender_concept = ex::sender_t;
    const throwing_copy* value;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> ex::completion_signatures<ex::set_value_t(const throwing_copy&)> { return {}; }

    auto get_env() const noexcept -> ex::empty_env { return {}; }

    template<ex::receiver R>
    struct operation : ex::__forge_detail::__immovable {
        using operation_state_concept = ex::operation_state_t;
        R receiver;
        const throwing_copy* value;

        operation(R r, const throwing_copy* v) : receiver(std::move(r)), value(v) {}
        void start() & noexcept { ex::set_value(std::move(receiver), *value); }
    };

    template<ex::receiver R>
    auto connect(R r) const -> operation<R> { return operation<R>{std::move(r), value}; }
};

} // namespace

TEST(ExecutionStoppedOptional, TypedStoppedCompletesWithEmptyOptionalInAllForms) {
    auto direct = ex::sync_wait(ex::stopped_as_optional(typed_sender<>{}));
    auto piped = ex::sync_wait(typed_sender<>{} | ex::stopped_as_optional);
    auto closure = ex::sync_wait(typed_sender<>{} | ex::stopped_as_optional());
    ASSERT_TRUE(direct.has_value());
    ASSERT_TRUE(piped.has_value());
    ASSERT_TRUE(closure.has_value());
    EXPECT_FALSE(std::get<0>(*direct).has_value());
    EXPECT_FALSE(std::get<0>(*piped).has_value());
    EXPECT_FALSE(std::get<0>(*closure).has_value());
}

TEST(ExecutionStoppedOptional, ValueCompletesWithAnRvalueOptional) {
    observed_result result;
    auto sender = ex::stopped_as_optional(typed_sender<>{completion::value});
    auto operation = ex::connect(std::move(sender), optional_receiver{&result});
    ex::start(operation);
    EXPECT_EQ(result.values, 1);
    EXPECT_EQ(result.errors, 0);
    EXPECT_TRUE(result.engaged);
    EXPECT_EQ(result.value, 42);
}

TEST(ExecutionStoppedOptional, SingleMultiParameterSignatureUsesOptionalTuple) {
    using adapted = decltype(ex::stopped_as_optional(typed_sender<true>{}));
    static_assert(std::is_same_v<ex::completion_signatures_of_t<adapted>,
        ex::completion_signatures<ex::set_value_t(std::optional<std::tuple<int, int>>),
            ex::set_error_t(int)>>);
    auto value = ex::sync_wait(ex::stopped_as_optional(typed_sender<true>{completion::value}));
    auto stopped = ex::sync_wait(ex::stopped_as_optional(typed_sender<true>{}));
    ASSERT_TRUE(value.has_value());
    ASSERT_TRUE(std::get<0>(*value).has_value());
    EXPECT_EQ(*std::get<0>(*value), (std::tuple{42, 43}));
    ASSERT_TRUE(stopped.has_value());
    EXPECT_FALSE(std::get<0>(*stopped).has_value());
}

TEST(ExecutionStoppedOptional, OneEmptyTuplePayloadIsNotAVoidSignature) {
    auto result = ex::sync_wait(ex::stopped_as_optional(ex::just(std::tuple<>{})));
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(std::get<0>(*result).has_value());
}

TEST(ExecutionStoppedOptional, PreservesMoveOnlyAndReferenceWrapperPayloads) {
    auto moved = ex::sync_wait(ex::stopped_as_optional(ex::just(std::make_unique<int>(42))));
    ASSERT_TRUE(moved.has_value());
    ASSERT_TRUE(std::get<0>(*moved).has_value());
    ASSERT_TRUE(*std::get<0>(*moved));
    EXPECT_EQ(**std::get<0>(*moved), 42);

    int value = 41;
    auto sender = ex::stopped_as_optional(ex::just(std::ref(value)));
    value = 42;
    auto borrowed = ex::sync_wait(std::move(sender));
    ASSERT_TRUE(borrowed.has_value());
    ASSERT_TRUE(std::get<0>(*borrowed).has_value());
    EXPECT_EQ(std::get<0>(*borrowed)->get(), 42);
    EXPECT_EQ(std::addressof(std::get<0>(*borrowed)->get()), std::addressof(value));
}

TEST(ExecutionStoppedOptional, ForwardsTheOriginalErrorWithoutWrappingIt) {
    observed_result result;
    auto operation = ex::connect(
        ex::stopped_as_optional(typed_sender<>{completion::error}), optional_receiver{&result});
    ex::start(operation);
    EXPECT_EQ(result.values, 0);
    EXPECT_EQ(result.errors, 1);
    EXPECT_EQ(result.value, 7);
}

TEST(ExecutionStoppedOptional, DependentEnvironmentIsCheckedAtConnect) {
    using source = typed_sender<false, true>;
    static_assert(!ex::sender_in<source>);
    for (auto outcome : {completion::value, completion::error, completion::stopped}) {
        for (bool copy_sender : {false, true}) {
            observed_result result;
            const auto sender = ex::stopped_as_optional(source{outcome});
            static_assert(!ex::sender_in<decltype(sender)>);
            static_assert(ex::sender_in<decltype(sender), optional_env>);
            if (copy_sender) {
                auto operation = ex::connect(sender, optional_receiver{&result});
                ex::start(operation);
            } else {
                auto operation = ex::connect(ex::stopped_as_optional(source{outcome}),
                    optional_receiver{&result});
                ex::start(operation);
            }
            EXPECT_EQ(result.values, outcome == completion::error ? 0 : 1);
            EXPECT_EQ(result.errors, outcome == completion::error ? 1 : 0);
            EXPECT_EQ(result.engaged, outcome == completion::value);
            EXPECT_EQ(result.value, outcome == completion::value ? 42 :
                outcome == completion::error ? 7 : 0);
        }
    }
}

TEST(ExecutionStoppedOptional, OptionalConstructionExceptionsBecomeExceptionPtrErrors) {
    const throwing_copy value(42, true);
    auto sender = ex::stopped_as_optional(reference_sender{&value});
    static_assert(std::is_same_v<ex::completion_signatures_of_t<decltype(sender)>,
        ex::completion_signatures<ex::set_value_t(std::optional<throwing_copy>),
            ex::set_error_t(std::exception_ptr)>>);
    EXPECT_THROW((void)ex::sync_wait(std::move(sender)), copy_failure);
}

TEST(ExecutionStoppedOptional, ReadSchedulerUsesTheActualSyncWaitEnvironment) {
    auto verify = [](auto sender) {
        auto result = ex::sync_wait(std::move(sender) | ex::then(
            [](std::optional<ex::run_loop::scheduler> scheduler) noexcept {
                return scheduler.has_value();
            }));
        ASSERT_TRUE(result.has_value());
        EXPECT_TRUE(std::get<0>(*result));
    };

    verify(ex::stopped_as_optional(ex::read_env(ex::get_scheduler)));
    verify(ex::read_env(ex::get_scheduler) | ex::stopped_as_optional);
    verify(ex::read_env(ex::get_scheduler) | ex::stopped_as_optional());
}

TEST(ExecutionStoppedOptional, EmptyEnvironmentVoidDoesNotRejectActualIntValue) {
    static_assert(std::is_void_v<std::invoke_result_t<empty_void_query&, ex::empty_env>>);
    static_assert(std::is_same_v<std::invoke_result_t<empty_void_query&, optional_env>, int>);

    auto verify = [](auto sender) {
        static_assert(std::is_same_v<ex::completion_signatures_of_t<decltype(sender), optional_env>,
            ex::completion_signatures<ex::set_value_t(std::optional<int>)>>);
        for (bool copy_sender : {false, true}) {
            observed_result result;
            if (copy_sender) {
                const auto& source = sender;
                auto operation = ex::connect(source, optional_receiver{&result});
                ex::start(operation);
            } else {
                auto source = sender;
                auto operation = ex::connect(std::move(source), optional_receiver{&result});
                ex::start(operation);
            }
            EXPECT_EQ(result.values, 1);
            EXPECT_EQ(result.errors, 0);
            EXPECT_TRUE(result.engaged);
            EXPECT_EQ(result.value, 42);
        }
    };

    verify(ex::stopped_as_optional(ex::read_env(empty_void_query{})));
    verify(ex::read_env(empty_void_query{}) | ex::stopped_as_optional);
    verify(ex::read_env(empty_void_query{}) | ex::stopped_as_optional());
}
