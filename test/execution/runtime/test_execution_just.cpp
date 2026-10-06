#include <gtest/gtest.h>

#include <execution>
#include <functional>
#include <type_traits>
#include <utility>

namespace {

namespace ex = std::execution;

struct copy_failure {};
struct move_failure {};

struct construction_counts {
    int copies = 0;
    int moves = 0;
    bool throw_on_copy = false;
    bool throw_on_move = false;
};

struct tracked_value {
    int value;
    construction_counts* counts;

    tracked_value(int v, construction_counts& c) noexcept : value(v), counts(&c) {}

    tracked_value(const tracked_value& other) : value(other.value), counts(other.counts) {
        ++counts->copies;
        if (counts->throw_on_copy) {
            throw copy_failure{};
        }
    }

    tracked_value(tracked_value&& other) noexcept(false)
        : value(other.value), counts(other.counts) {
        ++counts->moves;
        if (counts->throw_on_move) {
            throw move_failure{};
        }
        other.value = -1;
    }

    tracked_value& operator=(const tracked_value&) = delete;
    tracked_value& operator=(tracked_value&&) = delete;
};

struct mutable_copy {
    int value;
    explicit mutable_copy(int v) noexcept : value(v) {}
    mutable_copy(mutable_copy&&) = default;
    mutable_copy(mutable_copy& other) noexcept : value(other.value) {}
    mutable_copy(const mutable_copy&) = delete;
};

struct explicit_copy {
    int value;
    int* copies;
    explicit_copy(int v, int& c) noexcept : value(v), copies(&c) {}
    explicit_copy(explicit_copy&&) = default;
    explicit explicit_copy(const explicit_copy& other) noexcept
        : value(other.value), copies(other.copies) {
        ++*copies;
    }
};

struct completion_result {
    int values = 0;
    int errors = 0;
    int stopped = 0;
    int rvalues = 0;
    int lvalues = 0;
    int const_lvalues = 0;
    int value = 0;
};

int payload_value(const tracked_value& value) noexcept { return value.value; }
int payload_value(const mutable_copy& value) noexcept { return value.value; }
int payload_value(const explicit_copy& value) noexcept { return value.value; }
int payload_value(std::reference_wrapper<int> value) noexcept { return value.get(); }
int sample_function(int value) noexcept { return value + 1; }
int payload_value(decltype(&sample_function) value) noexcept { return value(10); }

template<class T>
struct observing_receiver {
    using receiver_concept = ex::receiver_t;
    completion_result* result;

    void set_value(T&& value) && noexcept {
        ++result->values;
        ++result->rvalues;
        result->value = payload_value(value);
    }
    void set_value(T& value) && noexcept {
        ++result->values;
        ++result->lvalues;
        result->value = payload_value(value);
    }
    void set_value(const T& value) && noexcept {
        ++result->values;
        ++result->const_lvalues;
        result->value = payload_value(value);
    }
    void set_error(T&& value) && noexcept {
        ++result->errors;
        ++result->rvalues;
        result->value = payload_value(value);
    }
    void set_error(T& value) && noexcept {
        ++result->errors;
        ++result->lvalues;
        result->value = payload_value(value);
    }
    void set_error(const T& value) && noexcept {
        ++result->errors;
        ++result->const_lvalues;
        result->value = payload_value(value);
    }
    void set_stopped() && noexcept { ++result->stopped; }
};

template<class T, bool Error, class S>
void check_completion(S&& sender, int expected) {
    completion_result result;
    auto op = ex::connect(std::forward<S>(sender), observing_receiver<T>{&result});
    ex::start(op);
    EXPECT_EQ(result.values, Error ? 0 : 1);
    EXPECT_EQ(result.errors, Error ? 1 : 0);
    EXPECT_EQ(result.stopped, 0);
    EXPECT_EQ(result.rvalues, 1);
    EXPECT_EQ(result.lvalues, 0);
    EXPECT_EQ(result.const_lvalues, 0);
    EXPECT_EQ(result.value, expected);
}

static_assert(std::move_constructible<tracked_value>);
static_assert(!std::movable<tracked_value>);
static_assert(!std::is_nothrow_move_constructible_v<tracked_value>);
static_assert(ex::receiver<observing_receiver<tracked_value>>);

} // namespace

TEST(ExecutionJust, CopiesLvaluesAndCompletesWithStoredRvalues) {
    construction_counts counts;
    tracked_value source(42, counts);
    auto sender = ex::just(source);
    EXPECT_EQ(counts.copies, 1);
    EXPECT_EQ(counts.moves, 0);
    source.value = 99;
    check_completion<tracked_value, false>(std::move(sender), 42);
}

TEST(ExecutionJust, ErrorCopiesLvaluesAndCompletesWithStoredRvalues) {
    construction_counts counts;
    tracked_value source(42, counts);
    auto sender = ex::just_error(source);
    EXPECT_EQ(counts.copies, 1);
    EXPECT_EQ(counts.moves, 0);
    source.value = 99;
    check_completion<tracked_value, true>(std::move(sender), 42);
}

TEST(ExecutionJust, BothFactoriesCopyConstLvalues) {
    construction_counts counts;
    const tracked_value source(42, counts);
    auto value_sender = ex::just(source);
    auto error_sender = ex::just_error(source);
    EXPECT_EQ(counts.copies, 2);
    EXPECT_EQ(counts.moves, 0);
    check_completion<tracked_value, false>(std::move(value_sender), 42);
    check_completion<tracked_value, true>(std::move(error_sender), 42);
    EXPECT_EQ(source.value, 42);
}

TEST(ExecutionJust, BothFactoriesAcceptPotentiallyThrowingNonAssignableMoves) {
    construction_counts counts;
    tracked_value value_source(42, counts);
    tracked_value error_source(43, counts);
    auto value_sender = ex::just(std::move(value_source));
    auto error_sender = ex::just_error(std::move(error_source));
    EXPECT_EQ(counts.copies, 0);
    EXPECT_EQ(counts.moves, 2);
    EXPECT_EQ(value_source.value, -1);
    EXPECT_EQ(error_source.value, -1);
    check_completion<tracked_value, false>(std::move(value_sender), 42);
    check_completion<tracked_value, true>(std::move(error_sender), 43);
}

TEST(ExecutionJust, BothFactoriesUseTheOriginalMutableLvalueCategory) {
    mutable_copy source(42);
    auto value_sender = ex::just(source);
    auto error_sender = ex::just_error(source);
    source.value = 99;
    check_completion<mutable_copy, false>(std::move(value_sender), 42);
    check_completion<mutable_copy, true>(std::move(error_sender), 42);
}

TEST(ExecutionJust, BothFactoriesPreserveReferenceWrappers) {
    int source = 42;
    auto value_sender = ex::just(std::ref(source));
    auto error_sender = ex::just_error(std::ref(source));
    source = 43;
    check_completion<std::reference_wrapper<int>, false>(std::move(value_sender), 43);
    check_completion<std::reference_wrapper<int>, true>(std::move(error_sender), 43);
}

TEST(ExecutionJust, BothFactoriesDirectlyConstructFromExplicitCopyLvalues) {
    int copies = 0;
    const explicit_copy source(42, copies);
    auto value_sender = ex::just(source);
    auto error_sender = ex::just_error(source);
    EXPECT_EQ(copies, 2);
    check_completion<explicit_copy, false>(std::move(value_sender), 42);
    check_completion<explicit_copy, true>(std::move(error_sender), 42);
    EXPECT_EQ(copies, 2);
}

TEST(ExecutionJust, BothFactoriesRetainValidFunctionAndPointerArguments) {
    check_completion<decltype(&sample_function), false>(ex::just(sample_function), 11);
    check_completion<decltype(&sample_function), true>(ex::just_error(sample_function), 11);
    check_completion<decltype(&sample_function), false>(ex::just(&sample_function), 11);
    check_completion<decltype(&sample_function), true>(ex::just_error(&sample_function), 11);
}

TEST(ExecutionJust, ConstructionExceptionsPropagateFromBothFactories) {
    construction_counts counts;
    tracked_value source(42, counts);
    counts.throw_on_copy = true;
    EXPECT_THROW((void)ex::just(source), copy_failure);
    EXPECT_THROW((void)ex::just_error(source), copy_failure);
    counts.throw_on_copy = false;
    counts.throw_on_move = true;
    EXPECT_THROW((void)ex::just(std::move(source)), move_failure);
    EXPECT_THROW((void)ex::just_error(std::move(source)), move_failure);
}
