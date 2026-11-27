// Frame ownership and terminal delivery of std::execution::task.
// Test names carry the pilot acceptance case they cover (F-01 to F-07).

#include <execution>

// The fixtures synchronize through the backport's private L0 shim, so the
// worker schedules below replay on the same seam as the library.
#include <cpp26/execution/detail/sync_shim.hpp>

#include "adversarial_schedulers.hpp"

#include <gtest/gtest.h>

#include <concepts>
#include <coroutine>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

namespace ex = std::execution;
namespace sync_ops = std::execution::__forge_sync;
using test_execution::adversarial_schedules;
using test_execution::schedule_kind;

// ---------------------------------------------------------------------------
// Shared fixtures

class trace {
public:
    void add(std::string event) {
        sync_ops::unique_lock lock{mutex_};
        events_.push_back(std::move(event));
    }

    std::vector<std::string> events() const {
        sync_ops::unique_lock lock{mutex_};
        return events_;
    }

    std::size_t count(const std::string& event) const {
        sync_ops::unique_lock lock{mutex_};
        std::size_t matches = 0;
        for (const auto& entry : events_) matches += entry == event ? 1 : 0;
        return matches;
    }

private:
    mutable sync_ops::mutex mutex_;
    std::vector<std::string> events_;
};

using events = std::vector<std::string>;

// A by-value coroutine parameter. Only the copy that ends up in the frame is
// still armed when it dies.
struct frame_marker {
    explicit frame_marker(trace& log) : log(&log) {}
    frame_marker(frame_marker&& other) noexcept : log(std::exchange(other.log, nullptr)) {}
    ~frame_marker() {
        if (log) log->add("frame-destroyed");
    }

    trace* log;
};

// Owns a heap operation so a receiver can destroy it inside its completion.
struct operation_slot {
    void* operation = nullptr;
    void (*destroy)(void*) noexcept = nullptr;
    trace* log = nullptr;

    template<class Sender, class Receiver>
    auto& emplace(Sender&& sender, Receiver&& receiver) {
        using operation_t = ex::connect_result_t<Sender, Receiver>;
        auto* created = new operation_t(
            ex::connect(std::forward<Sender>(sender), std::forward<Receiver>(receiver)));
        operation = created;
        destroy = [](void* target) noexcept { delete static_cast<operation_t*>(target); };
        return *created;
    }

    void reset() noexcept {
        if (auto* target = std::exchange(operation, nullptr)) {
            destroy(target);
            if (log) log->add("operation-destroyed");
        }
    }

    ~operation_slot() { reset(); }
};

struct sample_error {
    int code;
};

struct inline_environment {
    using start_scheduler_type = ex::inline_scheduler;
};

template<class... Errors>
struct error_environment {
    using start_scheduler_type = ex::inline_scheduler;
    using error_types = ex::completion_signatures<ex::set_error_t(Errors)...>;
};

// The default task erases its start scheduler, so the receiver supplies one.
using inline_start = ex::prop<ex::get_start_scheduler_t, ex::inline_scheduler>;
using erased_start = ex::prop<ex::get_start_scheduler_t, ex::task_scheduler>;

inline_start make_inline_start() {
    return ex::prop{ex::get_start_scheduler, ex::inline_scheduler{}};
}

erased_start make_erased_start(ex::task_scheduler scheduler) {
    return ex::prop{ex::get_start_scheduler, std::move(scheduler)};
}

// Completions go to a test-specific sink. With a doomed slot the receiver
// destroys its own operation right after the sink consumed the arguments.
struct sink_base {
    explicit sink_base(trace& log) : log(&log) {}

    template<class... Values>
    void value(Values&&...) { log->add("value"); }

    void error(std::exception_ptr failure) {
        saved = std::move(failure);
        log->add("error-exception");
    }

    template<class Error>
    void error(Error&&) { log->add("error-typed"); }

    void stopped() { log->add("stopped"); }

    trace* log;
    std::exception_ptr saved;
};

template<class Sink, class Env = ex::env<>>
struct sink_receiver {
    using receiver_concept = ex::receiver_tag;

    Sink* sink;
    Env env{};
    operation_slot* doomed = nullptr;

    auto get_env() const noexcept -> Env { return env; }

    template<class... Values>
    void set_value(Values&&... values) && noexcept {
        auto* const target = sink;
        auto* const slot = doomed;
        target->value(std::forward<Values>(values)...);
        if (slot) slot->reset();
    }

    template<class Error>
    void set_error(Error&& failure) && noexcept {
        auto* const target = sink;
        auto* const slot = doomed;
        target->error(std::forward<Error>(failure));
        if (slot) slot->reset();
    }

    void set_stopped() && noexcept {
        auto* const target = sink;
        auto* const slot = doomed;
        target->stopped();
        if (slot) slot->reset();
    }
};

template<class Sink>
sink_receiver(Sink*) -> sink_receiver<Sink>;
template<class Sink, class Env>
sink_receiver(Sink*, Env) -> sink_receiver<Sink, Env>;
template<class Sink, class Env>
sink_receiver(Sink*, Env, operation_slot*) -> sink_receiver<Sink, Env>;

int code_of(const std::exception_ptr& failure) {
    if (!failure) return -1;
    try {
        std::rethrow_exception(failure);
    } catch (const sample_error& error) {
        return error.code;
    } catch (...) {
        return -2;
    }
}

template<class T, class Environment = ex::env<>>
using signatures_of = ex::completion_signatures_of_t<ex::task<T, Environment>, ex::env<>>;

// [task.members] leaves the order of the signatures unspecified, so compare
// membership and count. Each expected signature is still an exact type.
template<class Signature, class List>
inline constexpr bool lists_signature = false;

template<class Signature, class... Listed>
inline constexpr bool lists_signature<Signature, ex::completion_signatures<Listed...>> =
    (std::same_as<Signature, Listed> || ...);

template<class List>
inline constexpr std::size_t signature_count = 0;

template<class... Listed>
inline constexpr std::size_t signature_count<ex::completion_signatures<Listed...>> = sizeof...(Listed);

template<class List, class... Expected>
concept signature_set =
    signature_count<List> == sizeof...(Expected) && (lists_signature<Expected, List> && ...);

// The helper itself tells order from content.
static_assert(signature_set<ex::completion_signatures<ex::set_stopped_t(), ex::set_value_t(int)>,
                            ex::set_value_t(int), ex::set_stopped_t()>);
static_assert(!signature_set<ex::completion_signatures<ex::set_stopped_t(), ex::set_value_t(int)>,
                             ex::set_value_t(int&), ex::set_stopped_t()>);
static_assert(!signature_set<ex::completion_signatures<ex::set_stopped_t(), ex::set_value_t(int)>,
                             ex::set_value_t(int)>);

// ---------------------------------------------------------------------------
// F-01: ownership trace

ex::task<int> counted_value(frame_marker, trace& log, int result) {
    log.add("body");
    co_return result;
}

ex::task<int, inline_environment> counted_inline_value(frame_marker, trace& log, int result) {
    log.add("body");
    co_return result;
}

struct int_sink : sink_base {
    using sink_base::sink_base;
    using sink_base::value;

    void value(int&& result) {
        received = result;
        log->add("value");
    }

    std::optional<int> received;
};

TEST(ExecutionTaskFrame, F01_CallCreatesSuspendedFrameAndDestructionReleasesItOnce) {
    trace log;
    {
        auto task = counted_value(frame_marker{log}, log, 1);
        EXPECT_EQ(log.count("body"), 0u);
        EXPECT_EQ(log.count("frame-destroyed"), 0u);
    }
    // A nonempty unconnected task destroys its frame.
    EXPECT_EQ(log.events(), (events{"frame-destroyed"}));
}

TEST(ExecutionTaskFrame, F01_MoveEmptiesTheSourceAndKeepsOneOwner) {
    trace log;
    {
        std::optional<ex::task<int>> source{counted_value(frame_marker{log}, log, 1)};
        ex::task<int> target{std::move(*source)};
        source.reset();
        // The moved-from task owned nothing.
        EXPECT_EQ(log.count("frame-destroyed"), 0u);
        EXPECT_EQ(log.count("body"), 0u);
    }
    EXPECT_EQ(log.events(), (events{"frame-destroyed"}));
}

TEST(ExecutionTaskFrame, F01_ConnectedButUnstartedOperationReleasesTheFrame) {
    trace log;
    int_sink sink{log};
    {
        auto task = counted_value(frame_marker{log}, log, 1);
        {
            auto operation = ex::connect(std::move(task), sink_receiver{&sink, make_inline_start()});
            EXPECT_EQ(log.count("frame-destroyed"), 0u);
        }
        EXPECT_EQ(log.events(), (events{"frame-destroyed"}));
    }
    // The connected-from task was empty: no second release and no terminal.
    EXPECT_EQ(log.events(), (events{"frame-destroyed"}));
    EXPECT_FALSE(sink.received);
}

TEST(ExecutionTaskFrame, F01_StartedInlineSuccessOwnsNoFrameAfterCompletion) {
    trace log;
    int_sink sink{log};
    {
        auto operation = ex::connect(counted_value(frame_marker{log}, log, 41),
                                     sink_receiver{&sink, make_inline_start()});
        EXPECT_EQ(log.count("body"), 0u);
        ex::start(operation);
        // The frame is gone before the receiver is entered, and the retained
        // operation does not release it again.
        EXPECT_EQ(log.events(), (events{"body", "frame-destroyed", "value"}));
        EXPECT_EQ(sink.received, 41);
    }
    EXPECT_EQ(log.events(), (events{"body", "frame-destroyed", "value"}));
}

ex::task<int> scheduler_reading_body(trace& log, ex::task_scheduler expected) {
    // SCHED is associated before the body first runs.
    auto current = co_await ex::read_env(ex::get_start_scheduler);
    log.add(current == expected ? "scheduler-associated" : "scheduler-mismatch");
    co_return 5;
}

ex::task<int, inline_environment> inline_scheduler_reading_body(trace& log) {
    auto current = co_await ex::read_env(ex::get_start_scheduler);
    static_assert(std::same_as<decltype(current), ex::inline_scheduler>);
    log.add(current == ex::inline_scheduler{} ? "scheduler-associated" : "scheduler-mismatch");
    co_return 6;
}

TEST(ExecutionTaskFrame, F01_StartAssociatesBeforeResumingDirectly) {
    {
        // A parked scheduler would hold back any extra initial schedule.
        trace log;
        int_sink sink{log};
        adversarial_schedules parked{{schedule_kind::never_resume}};
        const ex::task_scheduler erased{parked.get_scheduler()};
        auto operation = ex::connect(counted_value(frame_marker{log}, log, 3),
                                     sink_receiver{&sink, make_erased_start(erased)});
        ex::start(operation);
        EXPECT_EQ(parked.steps(), 0u);
        EXPECT_EQ(log.events(), (events{"body", "frame-destroyed", "value"}));
        EXPECT_EQ(sink.received, 3);
    }
    {
        trace log;
        int_sink sink{log};
        const ex::task_scheduler erased{ex::inline_scheduler{}};
        auto operation = ex::connect(scheduler_reading_body(log, erased),
                                     sink_receiver{&sink, make_erased_start(erased)});
        ex::start(operation);
        EXPECT_EQ(log.events(), (events{"scheduler-associated", "value"}));
        EXPECT_EQ(sink.received, 5);
    }
    {
        // An inline scheduler from the environment is erased at start.
        trace log;
        int_sink sink{log};
        auto operation = ex::connect(
            scheduler_reading_body(log, ex::task_scheduler{ex::inline_scheduler{}}),
            sink_receiver{&sink, make_inline_start()});
        ex::start(operation);
        EXPECT_EQ(log.events(), (events{"scheduler-associated", "value"}));
    }
    {
        trace log;
        int_sink sink{log};
        auto operation = ex::connect(inline_scheduler_reading_body(log), sink_receiver{&sink});
        ex::start(operation);
        EXPECT_EQ(log.events(), (events{"scheduler-associated", "value"}));
        EXPECT_EQ(sink.received, 6);
    }
}

// ---------------------------------------------------------------------------
// F-02: failed connect

struct connect_counters {
    int receivers = 0;
    int own_environments = 0;
    int environments = 0;
    bool fail_receiver = false;
    bool fail_own_environment = false;
    bool fail_environment = false;
};

struct connect_failure {
    int stage;
};

struct counters_env {
    connect_counters* counters;
};

struct counted_receiver {
    using receiver_concept = ex::receiver_tag;

    counted_receiver(connect_counters& counters, trace& log) : counters(&counters), log(&log) {
        ++counters.receivers;
    }
    counted_receiver(const counted_receiver& other) : counters(other.counters), log(other.log) {
        if (counters->fail_receiver) throw connect_failure{1};
        ++counters->receivers;
    }
    counted_receiver(counted_receiver&& other) noexcept : counters(other.counters), log(other.log) {
        ++counters->receivers;
    }
    ~counted_receiver() { --counters->receivers; }

    auto get_env() const noexcept -> counters_env { return {counters}; }

    void set_value(int) && noexcept { log->add("value"); }
    void set_error(std::exception_ptr) && noexcept { log->add("error-exception"); }
    void set_stopped() && noexcept { log->add("stopped"); }

    connect_counters* counters;
    trace* log;
};

struct counted_environment {
    using start_scheduler_type = ex::inline_scheduler;

    template<class ReceiverEnv>
    struct env_type {
        explicit env_type(counters_env source) : counters(source.counters) {
            if (counters->fail_own_environment) throw connect_failure{2};
            ++counters->own_environments;
        }
        env_type(const env_type&) = delete;
        ~env_type() { --counters->own_environments; }

        connect_counters* counters;
    };

    template<class ReceiverEnv>
    explicit counted_environment(env_type<ReceiverEnv>& own) : counters(own.counters) {
        if (counters->fail_environment) throw connect_failure{3};
        ++counters->environments;
    }
    counted_environment(const counted_environment&) = delete;
    ~counted_environment() { --counters->environments; }

    connect_counters* counters;
};

ex::task<int, counted_environment> counted_environment_value(frame_marker, trace& log) {
    log.add("body");
    co_return 9;
}

int run_failed_connect(connect_counters& counters, trace& log) {
    counted_receiver receiver{counters, log};
    auto task = counted_environment_value(frame_marker{log}, log);
    try {
        // The lvalue receiver is copied into the state after the frame moved.
        auto operation = ex::connect(std::move(task), receiver);
        ex::start(operation);
        return 0;
    } catch (const connect_failure& failure) {
        // The frame died with the half-built state, while the task is alive.
        EXPECT_EQ(log.events(), (events{"frame-destroyed"}));
        EXPECT_EQ(counters.receivers, 1);
        EXPECT_EQ(counters.own_environments, 0);
        EXPECT_EQ(counters.environments, 0);
        return failure.stage;
    }
}

TEST(ExecutionTaskFrame, F02_ThrowingMemberConstructionReleasesTheMovedFrameOnce) {
    for (int stage = 1; stage <= 3; ++stage) {
        SCOPED_TRACE(stage);
        trace log;
        connect_counters counters;
        counters.fail_receiver = stage == 1;
        counters.fail_own_environment = stage == 2;
        counters.fail_environment = stage == 3;
        EXPECT_EQ(run_failed_connect(counters, log), stage);
        // The emptied task and the unwound members released nothing further,
        // and no terminal ran.
        EXPECT_EQ(log.events(), (events{"frame-destroyed"}));
        EXPECT_EQ(counters.receivers, 0);
    }
}

TEST(ExecutionTaskFrame, F02_ControlConnectConstructsMembersInOrderAndCompletes) {
    trace log;
    connect_counters counters;
    EXPECT_EQ(run_failed_connect(counters, log), 0);
    EXPECT_EQ(log.events(), (events{"body", "frame-destroyed", "value"}));
    EXPECT_EQ(counters.receivers, 0);
    EXPECT_EQ(counters.own_environments, 0);
    EXPECT_EQ(counters.environments, 0);
}

struct plain_receiver {
    using receiver_concept = ex::receiver_tag;
    auto get_env() const noexcept { return make_inline_start(); }
    void set_value(int) && noexcept {}
    void set_error(std::exception_ptr) && noexcept {}
    void set_stopped() && noexcept {}
};

template<class Task, class Receiver>
concept connectable = requires(Task&& task, Receiver&& receiver) {
    ex::connect(std::forward<Task>(task), std::forward<Receiver>(receiver));
};

// Only an rvalue task connects. The lvalue forms are the paired rejections.
static_assert(connectable<ex::task<int>, plain_receiver>);
static_assert(connectable<ex::task<int>, plain_receiver&>);
static_assert(!connectable<ex::task<int>&, plain_receiver>);
static_assert(!connectable<const ex::task<int>&, plain_receiver>);
static_assert(!connectable<const ex::task<int>, plain_receiver>);
static_assert(!std::copy_constructible<ex::task<int>>);
static_assert(std::is_nothrow_move_constructible_v<ex::task<int>>);
static_assert(!std::is_move_assignable_v<ex::task<int>>);

using plain_operation = ex::connect_result_t<ex::task<int>, plain_receiver>;
static_assert(ex::operation_state<plain_operation>);
static_assert(!std::is_copy_constructible_v<plain_operation>);
static_assert(!std::is_move_constructible_v<plain_operation>);
static_assert(!std::is_copy_assignable_v<plain_operation>);
static_assert(!std::is_move_assignable_v<plain_operation>);
static_assert(noexcept(ex::start(std::declval<plain_operation&>())));

// ---------------------------------------------------------------------------
// F-03: results

struct item {
    int id;
};

struct pair_result {
    int first;
    int second;
};

int external_int = 17;
item external_item{23};

static_assert(signature_set<signatures_of<void>,
    ex::set_value_t(),
    ex::set_error_t(std::exception_ptr),
    ex::set_stopped_t()>);
static_assert(signature_set<signatures_of<int>,
    ex::set_value_t(int),
    ex::set_error_t(std::exception_ptr),
    ex::set_stopped_t()>);
static_assert(signature_set<signatures_of<const int&>,
    ex::set_value_t(const int&),
    ex::set_error_t(std::exception_ptr),
    ex::set_stopped_t()>);
static_assert(signature_set<signatures_of<item&&>,
    ex::set_value_t(item&&),
    ex::set_error_t(std::exception_ptr),
    ex::set_stopped_t()>);
static_assert(signature_set<signatures_of<std::nullopt_t>,
    ex::set_value_t(std::nullopt_t),
    ex::set_error_t(std::exception_ptr),
    ex::set_stopped_t()>);
static_assert(signature_set<signatures_of<int, error_environment<sample_error>>,
    ex::set_value_t(int),
    ex::set_error_t(sample_error),
    ex::set_stopped_t()>);
static_assert(signature_set<signatures_of<void, error_environment<>>,
    ex::set_value_t(),
    ex::set_stopped_t()>);
static_assert(ex::sender<ex::task<>>);
static_assert(std::same_as<ex::task<>, ex::task<void, ex::env<>>>);

// Records what the receiver was actually handed.
template<class Expected>
struct category_sink : sink_base {
    using sink_base::sink_base;

    template<class... Values>
    void value(Values&&... values) {
        log->add("value");
        arity = sizeof...(Values);
        exact = (std::same_as<Values&&, Expected> && ... && (sizeof...(Values) == 1));
        ((address = static_cast<const void*>(std::addressof(values))), ...);
    }

    std::size_t arity = 99;
    bool exact = false;
    const void* address = nullptr;
};

template<class Sender, class Sink>
void run_inline(Sender&& sender, Sink& sink) {
    auto operation = ex::connect(std::forward<Sender>(sender), sink_receiver{&sink, make_inline_start()});
    ex::start(operation);
}

template<class Sender, class Sink>
void run_self_destroying(Sender&& sender, Sink& sink, trace& log) {
    operation_slot slot;
    slot.log = &log;
    auto& operation = slot.emplace(std::forward<Sender>(sender),
                                   sink_receiver{&sink, make_inline_start(), &slot});
    ex::start(operation);
    EXPECT_EQ(slot.operation, nullptr);
}

ex::task<> returns_void(frame_marker, trace& log) {
    log.add("body");
    co_return;
}

ex::task<int> returns_int(frame_marker) { co_return 42; }

ex::task<std::unique_ptr<int>> returns_move_only(frame_marker) {
    co_return std::make_unique<int>(64);
}

ex::task<pair_result> returns_braced(frame_marker) { co_return {3, 4}; }

ex::task<int&> returns_lvalue(frame_marker) { co_return external_int; }

ex::task<const int&> returns_const_lvalue(frame_marker) { co_return external_int; }

ex::task<item&&> returns_xvalue(frame_marker) { co_return std::move(external_item); }

ex::task<std::nullopt_t> returns_nullopt(frame_marker) { co_return std::nullopt; }

ex::task<std::in_place_t> returns_in_place(frame_marker) { co_return std::in_place; }

TEST(ExecutionTaskFrame, F03_VoidCompletesWithNoDatumAfterTheFrame) {
    trace log;
    category_sink<void> sink{log};
    run_inline(returns_void(frame_marker{log}, log), sink);
    EXPECT_EQ(log.events(), (events{"body", "frame-destroyed", "value"}));
    EXPECT_EQ(sink.arity, 0u);
}

TEST(ExecutionTaskFrame, F03_ObjectResultsArriveAsRvaluesOfTheStateOwnedObject) {
    {
        trace log;
        int_sink sink{log};
        run_inline(returns_int(frame_marker{log}), sink);
        EXPECT_EQ(log.events(), (events{"frame-destroyed", "value"}));
        EXPECT_EQ(sink.received, 42);
    }
    {
        trace log;
        category_sink<int&&> sink{log};
        run_inline(returns_int(frame_marker{log}), sink);
        EXPECT_TRUE(sink.exact);
    }
    {
        trace log;
        category_sink<pair_result&&> sink{log};
        run_inline(returns_braced(frame_marker{log}), sink);
        EXPECT_EQ(log.events(), (events{"frame-destroyed", "value"}));
        EXPECT_TRUE(sink.exact);
    }
    {
        trace log;
        category_sink<std::nullopt_t&&> sink{log};
        run_inline(returns_nullopt(frame_marker{log}), sink);
        EXPECT_EQ(log.events(), (events{"frame-destroyed", "value"}));
        EXPECT_TRUE(sink.exact);
    }
    {
        trace log;
        category_sink<std::in_place_t&&> sink{log};
        run_inline(returns_in_place(frame_marker{log}), sink);
        EXPECT_EQ(log.events(), (events{"frame-destroyed", "value"}));
        EXPECT_TRUE(sink.exact);
    }
}

struct pair_sink : sink_base {
    using sink_base::sink_base;
    using sink_base::value;

    void value(pair_result&& result) {
        received = result;
        log->add("value");
    }

    std::optional<pair_result> received;
};

TEST(ExecutionTaskFrame, F03_BracedAggregateReturn) {
    trace log;
    pair_sink sink{log};
    run_inline(returns_braced(frame_marker{log}), sink);
    ASSERT_TRUE(sink.received);
    EXPECT_EQ(sink.received->first, 3);
    EXPECT_EQ(sink.received->second, 4);
}

struct move_only_sink : sink_base {
    using sink_base::sink_base;
    using sink_base::value;

    // Consume the state-owned argument before the operation may go away.
    void value(std::unique_ptr<int>&& result) {
        received = std::move(result);
        log->add("value");
    }

    std::unique_ptr<int> received;
};

TEST(ExecutionTaskFrame, F03_MoveOnlyResultAndReceiverSelfDestruction) {
    {
        trace log;
        move_only_sink sink{log};
        run_inline(returns_move_only(frame_marker{log}), sink);
        ASSERT_TRUE(sink.received);
        EXPECT_EQ(*sink.received, 64);
    }
    {
        trace log;
        move_only_sink sink{log};
        run_self_destroying(returns_move_only(frame_marker{log}), sink, log);
        EXPECT_EQ(log.events(), (events{"frame-destroyed", "value", "operation-destroyed"}));
        ASSERT_TRUE(sink.received);
        EXPECT_EQ(*sink.received, 64);
    }
    {
        trace log;
        category_sink<void> sink{log};
        run_self_destroying(returns_void(frame_marker{log}, log), sink, log);
        EXPECT_EQ(log.events(), (events{"body", "frame-destroyed", "value", "operation-destroyed"}));
    }
}

TEST(ExecutionTaskFrame, F03_ReferenceResultsKeepIdentityAndCategory) {
    {
        trace log;
        category_sink<int&> sink{log};
        run_inline(returns_lvalue(frame_marker{log}), sink);
        EXPECT_EQ(log.events(), (events{"frame-destroyed", "value"}));
        EXPECT_TRUE(sink.exact);
        EXPECT_EQ(sink.address, &external_int);
    }
    {
        trace log;
        category_sink<const int&> sink{log};
        run_inline(returns_const_lvalue(frame_marker{log}), sink);
        EXPECT_EQ(log.events(), (events{"frame-destroyed", "value"}));
        EXPECT_TRUE(sink.exact);
        EXPECT_EQ(sink.address, &external_int);
    }
    {
        trace log;
        category_sink<item&&> sink{log};
        run_inline(returns_xvalue(frame_marker{log}), sink);
        EXPECT_EQ(log.events(), (events{"frame-destroyed", "value"}));
        EXPECT_TRUE(sink.exact);
        EXPECT_EQ(sink.address, &external_item);
    }
    {
        // The referent is external, so the receiver may drop the operation.
        trace log;
        category_sink<item&&> sink{log};
        run_self_destroying(returns_xvalue(frame_marker{log}), sink, log);
        EXPECT_EQ(log.events(), (events{"frame-destroyed", "value", "operation-destroyed"}));
        EXPECT_EQ(sink.address, &external_item);
    }
}

// A second co_return, reached after a caught scope-exit exception, replaces
// the stored result as optional::emplace would.
struct tracked_counts {
    int live = 0;
    int constructed = 0;
    int destroyed = 0;
    int fail_value = 0;
};

struct replacement_failure {
    int value;
};

struct tracked {
    tracked(tracked_counts& counts, int value) : counts(&counts), value(value) { arrive(); }
    tracked(tracked&& other) : counts(other.counts), value(other.value) {
        if (counts->fail_value == value) throw replacement_failure{value};
        arrive();
    }
    ~tracked() {
        --counts->live;
        ++counts->destroyed;
    }

    void arrive() {
        ++counts->live;
        ++counts->constructed;
    }

    tracked_counts* counts;
    int value;
};

struct throwing_scope {
    ~throwing_scope() noexcept(false) { throw sample_error{9}; }
};

ex::task<tracked> returns_again_after_scope_exit(frame_marker, tracked_counts& counts, trace& log) {
    try {
        throwing_scope scope;
        co_return tracked{counts, 1};
    } catch (const sample_error&) {
        log.add("caught-scope-exit");
    }
    try {
        co_return tracked{counts, 2};
    } catch (const replacement_failure&) {
        log.add("caught-replacement");
    }
    co_return tracked{counts, 3};
}

ex::task<tracked> replacement_escapes(frame_marker, tracked_counts& counts, trace& log) {
    try {
        throwing_scope scope;
        co_return tracked{counts, 1};
    } catch (const sample_error&) {
        log.add("caught-scope-exit");
    }
    co_return tracked{counts, 2};
}

struct tracked_sink : sink_base {
    using sink_base::sink_base;
    using sink_base::value;

    void value(tracked&& result) {
        received = result.value;
        live_at_entry = result.counts->live;
        log->add("value");
    }

    std::optional<int> received;
    int live_at_entry = -1;
};

TEST(ExecutionTaskFrame, F03_SecondReturnReplacesTheStoredResult) {
    trace log;
    tracked_counts counts;
    tracked_sink sink{log};
    run_inline(returns_again_after_scope_exit(frame_marker{log}, counts, log), sink);
    EXPECT_EQ(log.events(), (events{"caught-scope-exit", "frame-destroyed", "value"}));
    EXPECT_EQ(sink.received, 2);
    // The first result was destroyed when the second replaced it.
    EXPECT_EQ(sink.live_at_entry, 1);
    EXPECT_EQ(counts.live, 0);
    EXPECT_EQ(counts.constructed, counts.destroyed);
}

TEST(ExecutionTaskFrame, F03_ThrowingReplacementLeavesTheSlotEmpty) {
    {
        // The body catches the failure and returns a third time.
        trace log;
        tracked_counts counts;
        counts.fail_value = 2;
        tracked_sink sink{log};
        run_inline(returns_again_after_scope_exit(frame_marker{log}, counts, log), sink);
        EXPECT_EQ(log.events(),
                  (events{"caught-scope-exit", "caught-replacement", "frame-destroyed", "value"}));
        EXPECT_EQ(sink.received, 3);
        EXPECT_EQ(sink.live_at_entry, 1);
        EXPECT_EQ(counts.live, 0);
        EXPECT_EQ(counts.constructed, counts.destroyed);
    }
    for (bool self_destroying : {false, true}) {
        // The failure escapes: no stale result is delivered or destroyed again.
        SCOPED_TRACE(self_destroying);
        trace log;
        tracked_counts counts;
        counts.fail_value = 2;
        tracked_sink sink{log};
        events expected{"caught-scope-exit", "frame-destroyed", "error-exception"};
        if (self_destroying) {
            run_self_destroying(replacement_escapes(frame_marker{log}, counts, log), sink, log);
            expected.push_back("operation-destroyed");
        } else {
            run_inline(replacement_escapes(frame_marker{log}, counts, log), sink);
        }
        EXPECT_EQ(log.events(), expected);
        EXPECT_FALSE(sink.received);
        ASSERT_TRUE(sink.saved);
        EXPECT_THROW(std::rethrow_exception(sink.saved), replacement_failure);
        EXPECT_EQ(counts.live, 0);
        EXPECT_EQ(counts.constructed, counts.destroyed);
    }
}

// ---------------------------------------------------------------------------
// F-04: body exceptions

ex::task<int> throws_sample(frame_marker, trace& log) {
    log.add("body");
    throw sample_error{7};
    co_return 0;
}

ex::task<int, error_environment<int>> throws_without_exception_channel(trace& log) {
    log.add("body");
    throw sample_error{8};
    co_return 0;
}

TEST(ExecutionTaskFrame, F04_BodyExceptionArrivesOnceAfterTheFrame) {
    trace log;
    int_sink sink{log};
    run_inline(throws_sample(frame_marker{log}, log), sink);
    EXPECT_EQ(log.events(), (events{"body", "frame-destroyed", "error-exception"}));
    EXPECT_EQ(code_of(sink.saved), 7);
    EXPECT_FALSE(sink.received);
}

TEST(ExecutionTaskFrame, F04_BodyExceptionWithReceiverSelfDestruction) {
    trace log;
    int_sink sink{log};
    run_self_destroying(throws_sample(frame_marker{log}, log), sink, log);
    EXPECT_EQ(log.events(),
              (events{"body", "frame-destroyed", "error-exception", "operation-destroyed"}));
    EXPECT_EQ(code_of(sink.saved), 7);
}

#if GTEST_HAS_DEATH_TEST
// Every receiver entry and every unexpected return has its own exit code.
struct exiting_sink {
    template<class... Values>
    void value(Values&&...) { std::_Exit(91); }
    template<class Error>
    void error(Error&&) { std::_Exit(92); }
    void stopped() { std::_Exit(93); }
};

trace* death_log = nullptr;

[[noreturn]] void terminate_on_body_exception() {
    trace log;
    exiting_sink sink;
    death_log = &log;
    std::set_terminate([] {
        // unhandled_exception terminates while the frame is still alive.
        std::_Exit(death_log->events() == events{"body"} ? 86 : 90);
    });
    auto operation = ex::connect(throws_without_exception_channel(log), sink_receiver{&sink});
    ex::start(operation);
    std::_Exit(94);
}

TEST(ExecutionTaskFrameDeathTest, F04_BodyExceptionWithoutExceptionChannelTerminates) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    EXPECT_EXIT(terminate_on_body_exception(), ::testing::ExitedWithCode(86), "");
}
#endif

// ---------------------------------------------------------------------------
// F-05: typed errors through co_yield with_error

struct staging_failure {
    int move;
};

struct conversion_failure {};

struct payload_controls {
    enum class phase { building, armed, staging };

    trace* log;
    int moves = 0;
    int conversions = 0;
    int fail_move = 0;
    bool fail_conversion = false;
    phase stage = phase::building;
};

// The coroutine builds a named with_error, arms the controls and yields the
// name as an xvalue. The first move after arming therefore initializes the
// by-value yield_value parameter on every compiler, which gives the count a
// portable starting point. Object 0 is that parameter, object
// 1 the target of the first staging move and object 2 the target of the
// second. Only the two staging moves are counted and may be made to throw.
struct payload {
    explicit payload(payload_controls& controls, int code) : controls(&controls), code(code), id(-1) {}
    payload(payload&& other) : controls(other.controls), code(other.code), id(-1) {
        using phase = payload_controls::phase;
        if (controls->stage == phase::building) return;
        if (controls->stage == phase::armed) {
            controls->stage = phase::staging;
            id = 0;
            controls->log->add("payload-parameter");
            return;
        }
        if (controls->fail_move == controls->moves + 1) throw staging_failure{controls->moves + 1};
        ++controls->moves;
        id = other.id + 1;
        controls->log->add("payload-move");
    }
    ~payload() {
        if (id >= 0) controls->log->add("payload-" + std::to_string(id) + "-destroyed");
    }

    payload_controls* controls;
    int code;
    int id;
};

// The protocol events without the payload lifetimes, which are checked apart:
// where a by-value parameter dies differs between ABIs.
events protocol_events(const trace& log) {
    events filtered;
    for (auto& entry : log.events()) {
        if (!(entry.starts_with("payload-") && entry.ends_with("-destroyed"))) filtered.push_back(entry);
    }
    return filtered;
}

std::ptrdiff_t position_of(const trace& log, const std::string& event) {
    const auto all = log.events();
    for (std::size_t index = 0; index != all.size(); ++index) {
        if (all[index] == event) return static_cast<std::ptrdiff_t>(index);
    }
    return -1;
}

// The first staging target lives in the frame and dies with it, before the
// receiver is entered. The second lives on the await_suspend stack: it
// outlives the frame, the conversion and the receiver's completion.
void expect_staging_lifetimes(const trace& log, const std::string& terminal) {
    const auto frame = position_of(log, "frame-destroyed");
    const auto entry = position_of(log, terminal);
    ASSERT_GE(frame, 0);
    ASSERT_GT(entry, frame);
    EXPECT_GE(position_of(log, "payload-0-destroyed"), 0);
    EXPECT_LT(position_of(log, "payload-0-destroyed"), frame);
    EXPECT_GE(position_of(log, "payload-1-destroyed"), 0);
    EXPECT_LT(position_of(log, "payload-1-destroyed"), frame);
    EXPECT_GT(position_of(log, "payload-2-destroyed"), entry);
    EXPECT_EQ(position_of(log, "payload-3-destroyed"), -1);
}

// The declared error type, reached from the payload by one implicit conversion.
struct wide_error {
    wide_error(payload&& source) : code(source.code) {
        // Record the attempt first, so a failing conversion is also placed.
        ++source.controls->conversions;
        source.controls->log->add("conversion");
        if (source.controls->fail_conversion) throw conversion_failure{};
    }
    wide_error(wide_error&&) = default;

    int code;
};

using typed_environment = error_environment<std::exception_ptr, wide_error>;
using typed_only_environment = error_environment<wide_error>;

static_assert(signature_set<signatures_of<void, typed_environment>,
    ex::set_value_t(),
    ex::set_error_t(std::exception_ptr),
    ex::set_error_t(wide_error),
    ex::set_stopped_t()>);

template<class Environment>
ex::task<void, Environment> yields_payload(frame_marker, payload_controls& controls) {
    controls.log->add("body");
    ex::with_error<payload> named{payload{controls, 31}};
    controls.stage = payload_controls::phase::armed;
    co_yield std::move(named);
    controls.log->add("after-yield");
}

struct wide_sink : sink_base {
    using sink_base::sink_base;
    using sink_base::error;

    void error(wide_error&& failure) {
        code = failure.code;
        log->add("error-wide");
    }

    void error(std::exception_ptr failure) {
        // A routed conversion failure arrives outside any handler.
        handler_active = static_cast<bool>(std::current_exception());
        uncaught = std::uncaught_exceptions();
        sink_base::error(std::move(failure));
    }

    int code = 0;
    bool handler_active = true;
    int uncaught = -1;
};

TEST(ExecutionTaskFrame, F05_TwoStagingMovesThenFrameThenOneConversion) {
    for (bool self_destroying : {false, true}) {
        SCOPED_TRACE(self_destroying);
        trace log;
        payload_controls controls{&log};
        wide_sink sink{log};
        events expected{"body", "payload-parameter", "payload-move", "payload-move", "frame-destroyed",
                        "conversion", "error-wide"};
        if (self_destroying) {
            run_self_destroying(yields_payload<typed_environment>(frame_marker{log}, controls), sink, log);
            expected.push_back("operation-destroyed");
        } else {
            run_inline(yields_payload<typed_environment>(frame_marker{log}, controls), sink);
        }
        EXPECT_EQ(protocol_events(log), expected);
        expect_staging_lifetimes(log, "error-wide");
        if (self_destroying) {
            EXPECT_GT(position_of(log, "payload-2-destroyed"), position_of(log, "operation-destroyed"));
        }
        EXPECT_EQ(controls.moves, 2);
        EXPECT_EQ(controls.conversions, 1);
        EXPECT_EQ(sink.code, 31);
        EXPECT_FALSE(sink.saved);
    }
}

TEST(ExecutionTaskFrame, F05_TypedErrorWithoutExceptionChannel) {
    trace log;
    payload_controls controls{&log};
    wide_sink sink{log};
    run_inline(yields_payload<typed_only_environment>(frame_marker{log}, controls), sink);
    EXPECT_EQ(protocol_events(log),
              (events{"body", "payload-parameter", "payload-move", "payload-move", "frame-destroyed",
                      "conversion", "error-wide"}));
    expect_staging_lifetimes(log, "error-wide");
    EXPECT_EQ(sink.code, 31);
}

TEST(ExecutionTaskFrame, F05_ThrowingStagingMoveTakesTheLiveFramePath) {
    for (int failing_move : {1, 2}) {
        SCOPED_TRACE(failing_move);
        trace log;
        payload_controls controls{&log};
        controls.fail_move = failing_move;
        wide_sink sink{log};
        run_inline(yields_payload<typed_environment>(frame_marker{log}, controls), sink);
        // The exception escapes the body, which then completes through
        // unhandled_exception like any other body exception.
        events expected{"body", "payload-parameter"};
        for (int done = 1; done < failing_move; ++done) expected.push_back("payload-move");
        expected.insert(expected.end(), {"frame-destroyed", "error-exception"});
        EXPECT_EQ(protocol_events(log), expected);
        // Every payload object died while the frame was alive.
        EXPECT_EQ(log.events().back(), "error-exception");
        EXPECT_EQ(controls.conversions, 0);
        ASSERT_TRUE(sink.saved);
        try {
            std::rethrow_exception(sink.saved);
        } catch (const staging_failure& failure) {
            EXPECT_EQ(failure.move, failing_move);
        } catch (...) {
            ADD_FAILURE() << "wrong exception";
        }
    }
}

template<class Environment>
ex::task<int, Environment> catches_staging_failure(frame_marker, payload_controls& controls) {
    int caught = -1;
    try {
        ex::with_error<payload> named{payload{controls, 32}};
        controls.stage = payload_controls::phase::armed;
        co_yield std::move(named);
        controls.log->add("after-yield");
    } catch (const staging_failure& failure) {
        // The frame is alive, so the body may handle the failure and go on.
        controls.log->add("caught");
        caught = failure.move;
    }
    co_return caught;
}

TEST(ExecutionTaskFrame, F05_BodyMayCatchAThrowingStagingMove) {
    for (int failing_move : {1, 2}) {
        SCOPED_TRACE(failing_move);
        trace log;
        payload_controls controls{&log};
        controls.fail_move = failing_move;
        int_sink sink{log};
        run_inline(catches_staging_failure<typed_environment>(frame_marker{log}, controls), sink);
        EXPECT_EQ(log.count("caught"), 1u);
        EXPECT_EQ(log.count("after-yield"), 0u);
        EXPECT_EQ(sink.received, failing_move);
        EXPECT_EQ(controls.conversions, 0);
    }
}

TEST(ExecutionTaskFrame, F05_ConversionFailureAfterTheFrameUsesTheExceptionChannel) {
    for (bool self_destroying : {false, true}) {
        SCOPED_TRACE(self_destroying);
        trace log;
        payload_controls controls{&log};
        controls.fail_conversion = true;
        wide_sink sink{log};
        // Exactly one conversion attempt, after the frame and before the entry.
        events expected{"body", "payload-parameter", "payload-move", "payload-move", "frame-destroyed",
                        "conversion", "error-exception"};
        if (self_destroying) {
            run_self_destroying(yields_payload<typed_environment>(frame_marker{log}, controls), sink, log);
            expected.push_back("operation-destroyed");
        } else {
            run_inline(yields_payload<typed_environment>(frame_marker{log}, controls), sink);
        }
        EXPECT_EQ(protocol_events(log), expected);
        expect_staging_lifetimes(log, "error-exception");
        EXPECT_EQ(controls.conversions, 1);
        EXPECT_EQ(log.count("conversion"), 1u);
        EXPECT_FALSE(sink.handler_active);
        EXPECT_EQ(sink.uncaught, 0);
        ASSERT_TRUE(sink.saved);
        EXPECT_THROW(std::rethrow_exception(sink.saved), conversion_failure);
    }
}

// The payload type is itself the declared error type.
ex::task<void, error_environment<sample_error>> yields_same_type(frame_marker) {
    co_yield ex::with_error{sample_error{12}};
}

struct sample_sink : sink_base {
    using sink_base::sink_base;
    using sink_base::error;

    void error(sample_error&& failure) {
        code = failure.code;
        log->add("error-sample");
    }

    int code = 0;
};

ex::task<void, error_environment<sample_error>> yields_lvalue_operand(frame_marker) {
    const sample_error named{13};
    co_yield ex::with_error{named};
}

TEST(ExecutionTaskFrame, F05_SameTypePayloadAndLvalueOperand) {
    {
        trace log;
        sample_sink sink{log};
        run_inline(yields_same_type(frame_marker{log}), sink);
        EXPECT_EQ(log.events(), (events{"frame-destroyed", "error-sample"}));
        EXPECT_EQ(sink.code, 12);
    }
    {
        trace log;
        sample_sink sink{log};
        run_inline(yields_lvalue_operand(frame_marker{log}), sink);
        EXPECT_EQ(log.events(), (events{"frame-destroyed", "error-sample"}));
        EXPECT_EQ(sink.code, 13);
    }
}

// A function reference is a valid error type. Its address is not an object
// pointer, so the erased delivery must not pass it through void*.
void first_handler() {}
void second_handler() {}
using handler_reference = void (&)();

struct handler_choice {
    operator handler_reference() const noexcept { return second_handler; }
};

using handler_environment = error_environment<std::exception_ptr, handler_reference>;

static_assert(signature_set<signatures_of<int, handler_environment>,
    ex::set_value_t(int),
    ex::set_error_t(std::exception_ptr),
    ex::set_error_t(handler_reference),
    ex::set_stopped_t()>);

ex::task<int, handler_environment> value_beside_handler_channel(frame_marker) { co_return 4; }

ex::task<int, handler_environment> yields_handler(frame_marker) {
    co_yield ex::with_error{handler_choice{}};
    co_return 0;
}

struct handler_sink : sink_base {
    using sink_base::sink_base;
    using sink_base::error;
    using sink_base::value;

    void value(int&& result) {
        received = result;
        log->add("value");
    }

    void error(handler_reference target) {
        referent = &target;
        log->add("error-handler");
    }

    std::optional<int> received;
    void (*referent)() = nullptr;
};

TEST(ExecutionTaskFrame, F05_FunctionReferenceErrorKeepsItsReferent) {
    {
        // Declaring the channel does not disturb a value completion.
        trace log;
        handler_sink sink{log};
        run_inline(value_beside_handler_channel(frame_marker{log}), sink);
        EXPECT_EQ(log.events(), (events{"frame-destroyed", "value"}));
        EXPECT_EQ(sink.received, 4);
    }
    for (bool self_destroying : {false, true}) {
        SCOPED_TRACE(self_destroying);
        trace log;
        handler_sink sink{log};
        events expected{"frame-destroyed", "error-handler"};
        if (self_destroying) {
            run_self_destroying(yields_handler(frame_marker{log}), sink, log);
            expected.push_back("operation-destroyed");
        } else {
            run_inline(yields_handler(frame_marker{log}), sink);
        }
        EXPECT_EQ(log.events(), expected);
        EXPECT_EQ(sink.referent, &second_handler);
        EXPECT_NE(sink.referent, &first_handler);
    }
}

static_assert(std::same_as<ex::with_error<const sample_error&>::type, sample_error>);
static_assert(std::same_as<decltype(ex::with_error{std::declval<const sample_error&>()}),
                           ex::with_error<sample_error>>);
static_assert(std::is_aggregate_v<ex::with_error<sample_error>>);

#if GTEST_HAS_DEATH_TEST
payload_controls* death_controls = nullptr;

[[noreturn]] void terminate_on_conversion_failure() {
    trace log;
    payload_controls controls{&log};
    controls.fail_conversion = true;
    exiting_sink sink;
    death_log = &log;
    death_controls = &controls;
    std::set_terminate([] {
        // Both staging moves and the frame destruction came first, then one
        // conversion attempt and no receiver entry.
        const bool ordered = protocol_events(*death_log) ==
            events{"body", "payload-parameter", "payload-move", "payload-move", "frame-destroyed",
                   "conversion"};
        std::_Exit(ordered && death_controls->conversions == 1 ? 86 : 90);
    });
    auto operation = ex::connect(yields_payload<typed_only_environment>(frame_marker{log}, controls),
                                 sink_receiver{&sink});
    ex::start(operation);
    std::_Exit(94);
}

TEST(ExecutionTaskFrameDeathTest, F05_ConversionFailureWithoutExceptionChannelTerminates) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    EXPECT_EXIT(terminate_on_conversion_failure(), ::testing::ExitedWithCode(86), "");
}
#endif

// ---------------------------------------------------------------------------
// F-06 and F-07: a controlled child

// The child completes inline in start, later on a harness call, or on a
// worker thread. The control block outlives every operation.
struct child_control {
    enum class mode { inline_completion, parked, worker_before_return, worker_after_return };
    enum class signal { stopped, value };

    explicit child_control(mode how, signal what, trace& log) : how(how), what(what), log(&log) {}

    ~child_control() {
        if (worker.joinable()) worker.join();
    }

    void release() {
        std::function<void()> pending;
        {
            sync_ops::unique_lock lock{mutex};
            pending = std::move(complete);
            complete = nullptr;
        }
        ASSERT_TRUE(pending);
        if (how == mode::parked) {
            pending();
        } else {
            worker = sync_ops::thread(std::move(pending));
        }
    }

    // Harness thread only. The worker must not hold the last reference.
    void wait_for_terminal() {
        {
            sync_ops::unique_lock lock{mutex};
            finished.wait(lock, [this] { return terminal_returned; });
        }
        if (worker.joinable()) worker.join();
    }

    void mark_terminal_returned() {
        {
            sync_ops::unique_lock lock{mutex};
            terminal_returned = true;
        }
        finished.notify_all();
    }

    const mode how;
    const signal what;
    trace* log;
    sync_ops::mutex mutex;
    sync_ops::condition_variable finished;
    std::function<void()> complete;
    bool terminal_returned = false;
    sync_ops::thread worker;
};

template<class Receiver>
struct child_operation {
    using operation_state_concept = ex::operation_state_tag;

    child_operation(std::shared_ptr<child_control> control, Receiver receiver)
        : control(std::move(control)), receiver(std::move(receiver)) {}
    child_operation(child_operation&&) = delete;

    ~child_operation() { control->log->add("child-destroyed"); }

    void start() & noexcept {
        // Completion can destroy this operation: keep the control on the stack.
        const auto shared = control;
        shared->log->add("child-start");
        if (shared->how == child_control::mode::inline_completion) {
            complete(this, *shared);
            shared->log->add("child-start-returns");
            return;
        }
        {
            sync_ops::unique_lock lock{shared->mutex};
            shared->complete = [self = this, shared] {
                complete(self, *shared);
                shared->mark_terminal_returned();
            };
        }
        if (shared->how == child_control::mode::worker_before_return) {
            // The whole terminal runs on the worker before this call returns.
            shared->release();
            shared->wait_for_terminal();
        }
        shared->log->add("child-start-returns");
    }

    static void complete(child_operation* self, child_control& shared) noexcept {
        if (shared.what == child_control::signal::stopped) {
            shared.log->add("child-stopped");
            ex::set_stopped(std::move(self->receiver));
        } else {
            shared.log->add("child-value");
            ex::set_value(std::move(self->receiver), 77);
        }
    }

    std::shared_ptr<child_control> control;
    Receiver receiver;
};

struct child_sender {
    using sender_concept = ex::sender_tag;

    template<class Self, class... Env>
    static consteval auto get_completion_signatures() noexcept {
        return ex::completion_signatures<ex::set_value_t(int), ex::set_stopped_t()>{};
    }

    template<ex::receiver Receiver>
    auto connect(Receiver receiver) && -> child_operation<Receiver> {
        return {std::move(control), std::move(receiver)};
    }

    std::shared_ptr<child_control> control;
};

template<class Environment>
ex::task<int, Environment> awaits_child(frame_marker, std::shared_ptr<child_control> control) {
    auto* const log = control->log;
    log->add("body");
    const int result = co_await child_sender{std::move(control)};
    log->add("after-await");
    co_return result;
}

template<class Environment>
auto child_receiver_env() {
    if constexpr (std::same_as<Environment, inline_environment>) {
        return ex::env<>{};
    } else {
        return make_inline_start();
    }
}

template<class Environment>
class ChildTaskTest : public ::testing::Test {};

using child_environments = ::testing::Types<inline_environment, ex::env<>>;
TYPED_TEST_SUITE(ChildTaskTest, child_environments);

events stopped_events(bool self_destroying, bool child_returns_last) {
    // The frame, and with it the child operation, dies before stopped is
    // delivered. The child's start is still on the stack in the inline case.
    events expected{"body", "child-start", "child-stopped", "child-destroyed", "frame-destroyed", "stopped"};
    if (self_destroying) expected.push_back("operation-destroyed");
    if (child_returns_last) expected.push_back("child-start-returns");
    return expected;
}

TYPED_TEST(ChildTaskTest, F06_ChildStoppedInsideStart) {
    for (bool self_destroying : {false, true}) {
        SCOPED_TRACE(self_destroying);
        trace log;
        int_sink sink{log};
        auto control = std::make_shared<child_control>(
            child_control::mode::inline_completion, child_control::signal::stopped, log);
        operation_slot slot;
        slot.log = &log;
        auto& operation = slot.emplace(
            awaits_child<TypeParam>(frame_marker{log}, control),
            sink_receiver{&sink, child_receiver_env<TypeParam>(), self_destroying ? &slot : nullptr});
        ex::start(operation);
        EXPECT_EQ(log.events(), stopped_events(self_destroying, true));
        EXPECT_EQ(log.count("after-await"), 0u);
        EXPECT_FALSE(sink.received);
    }
}

TYPED_TEST(ChildTaskTest, F06_ParkedChildHasNoTerminalUntilReleased) {
    for (bool self_destroying : {false, true}) {
        SCOPED_TRACE(self_destroying);
        trace log;
        int_sink sink{log};
        auto control = std::make_shared<child_control>(
            child_control::mode::parked, child_control::signal::stopped, log);
        operation_slot slot;
        slot.log = &log;
        auto& operation = slot.emplace(
            awaits_child<TypeParam>(frame_marker{log}, control),
            sink_receiver{&sink, child_receiver_env<TypeParam>(), self_destroying ? &slot : nullptr});
        ex::start(operation);
        // Parked: neither a terminal nor any destruction.
        EXPECT_EQ(log.events(), (events{"body", "child-start", "child-start-returns"}));
        control->release();
        events expected{"body", "child-start", "child-start-returns", "child-stopped",
                        "child-destroyed", "frame-destroyed", "stopped"};
        if (self_destroying) expected.push_back("operation-destroyed");
        EXPECT_EQ(log.events(), expected);
        EXPECT_EQ(log.count("after-await"), 0u);
    }
}

TYPED_TEST(ChildTaskTest, F06_ChildStoppedFromALaterWorkerCallback) {
    for (bool self_destroying : {false, true}) {
        SCOPED_TRACE(self_destroying);
        trace log;
        int_sink sink{log};
        auto control = std::make_shared<child_control>(
            child_control::mode::worker_after_return, child_control::signal::stopped, log);
        operation_slot slot;
        slot.log = &log;
        auto& operation = slot.emplace(
            awaits_child<TypeParam>(frame_marker{log}, control),
            sink_receiver{&sink, child_receiver_env<TypeParam>(), self_destroying ? &slot : nullptr});
        ex::start(operation);
        EXPECT_EQ(log.events(), (events{"body", "child-start", "child-start-returns"}));
        control->release();
        control->wait_for_terminal();
        events expected{"body", "child-start", "child-start-returns", "child-stopped",
                        "child-destroyed", "frame-destroyed", "stopped"};
        if (self_destroying) expected.push_back("operation-destroyed");
        EXPECT_EQ(log.events(), expected);
        EXPECT_EQ(log.count("after-await"), 0u);
    }
}

// The default-environment cases above await through affine's wrapper around
// task_scheduler, which compiles only while the eraser's mixed comparison
// stays out of that wrapper's scheduler check. These are the comparisons the
// guard must keep, in both orders.
struct derived_eraser : ex::task_scheduler {
    using ex::task_scheduler::task_scheduler;
};

TEST(ExecutionTaskFrame, F06_EraserComparisonsSurviveTheWrapperGuard) {
    adversarial_schedules first{{schedule_kind::never_resume}};
    adversarial_schedules second{{schedule_kind::never_resume}};
    const auto wrapped = first.get_scheduler();
    const auto other = second.get_scheduler();
    const ex::task_scheduler erased{wrapped};
    const derived_eraser derived{wrapped};

    EXPECT_TRUE(erased == wrapped);
    EXPECT_TRUE(wrapped == erased);
    EXPECT_FALSE(erased != wrapped);
    EXPECT_FALSE(erased == other);
    EXPECT_FALSE(other == erased);
    EXPECT_TRUE(erased != other);
    EXPECT_FALSE(erased == ex::inline_scheduler{});
    EXPECT_FALSE(ex::inline_scheduler{} == erased);

    // A publicly derived eraser binds on the left as it does in the draft.
    EXPECT_TRUE(derived == wrapped);
    EXPECT_TRUE(wrapped == derived);
    EXPECT_FALSE(derived == other);
    EXPECT_FALSE(other == derived);
    EXPECT_TRUE(derived != other);
    EXPECT_FALSE(derived == ex::inline_scheduler{});

    // Erased against erased keeps using the non-template comparison.
    EXPECT_TRUE(erased == ex::task_scheduler{wrapped});
    EXPECT_FALSE(erased == ex::task_scheduler{other});
    const ex::task_scheduler& base = derived;
    EXPECT_TRUE(erased == base);
}

// A concrete scheduler with its own comparison against an eraser. The
// eraser's mixed operator== has the draft's fixed left parameter, so for a
// derived eraser both candidates convert the left operand alike and the
// non-template one wins. A deduced left operand would bind exactly and let
// the eraser's template win instead, with the opposite result.
struct ranked_scheduler {
    using scheduler_concept = ex::scheduler_tag;

    struct sender {
        using sender_concept = ex::sender_tag;

        template<class Self, class... Env>
        static consteval auto get_completion_signatures() noexcept {
            return ex::completion_signatures<ex::set_value_t()>{};
        }

        template<ex::receiver Receiver>
        auto connect(Receiver receiver) const {
            return ex::connect(ex::schedule(ex::inline_scheduler{}), std::move(receiver));
        }
    };

    sender schedule() const noexcept { return {}; }
    auto query(ex::get_forward_progress_guarantee_t) const noexcept {
        return ex::forward_progress_guarantee::weakly_parallel;
    }
    bool operator==(const ranked_scheduler&) const noexcept = default;

    friend bool operator==(const ex::task_scheduler&, const ranked_scheduler& self) noexcept {
        ++*self.own_comparisons;
        return false;
    }

    int* own_comparisons;
};

// A publicly derived eraser whose scheduling and progress query also work on
// a volatile object, so that volatile-qualified it is still a scheduler. The
// eraser-to-eraser comparison cannot bind a volatile operand. The mixed
// comparison must therefore stay viable and report it as a different
// concrete type, as the draft's declaration does.
struct volatile_scheduler : ex::task_scheduler {
    using ex::task_scheduler::task_scheduler;

    auto schedule() const volatile noexcept { return ex::schedule(ex::inline_scheduler{}); }
    auto query(ex::get_forward_progress_guarantee_t) const volatile noexcept {
        return ex::forward_progress_guarantee::weakly_parallel;
    }
};

static_assert(ex::scheduler<volatile_scheduler>);
static_assert(ex::scheduler<volatile volatile_scheduler>);

template<class Left, class Right>
concept comparable_as = requires(const Left& left, const Right& right) {
    { left == right } -> std::same_as<bool>;
    { left != right } -> std::same_as<bool>;
};

static_assert(comparable_as<ex::task_scheduler, volatile volatile_scheduler>);
static_assert(comparable_as<volatile volatile_scheduler, ex::task_scheduler>);
static_assert(comparable_as<derived_eraser, volatile volatile_scheduler>);
static_assert(comparable_as<volatile volatile_scheduler, derived_eraser>);

TEST(ExecutionTaskFrame, F06_VolatileDerivedSchedulerStaysAConcreteOperand) {
    const ex::task_scheduler erased{ex::inline_scheduler{}};
    const derived_eraser derived{ex::inline_scheduler{}};
    volatile volatile_scheduler qualified{ex::inline_scheduler{}};
    const volatile_scheduler unqualified{ex::inline_scheduler{}};

    // The stored concrete type is never volatile, so these are unequal.
    EXPECT_FALSE(erased == qualified);
    EXPECT_FALSE(qualified == erased);
    EXPECT_TRUE(erased != qualified);
    EXPECT_TRUE(qualified != erased);
    EXPECT_FALSE(derived == qualified);
    EXPECT_FALSE(qualified == derived);

    // Without the qualifier it is an eraser and compares as one.
    EXPECT_TRUE(erased == unqualified);
    EXPECT_TRUE(unqualified == erased);
}

TEST(ExecutionTaskFrame, F06_EraserComparisonKeepsTheDraftRanking) {
    int own_comparisons = 0;
    const ranked_scheduler concrete{&own_comparisons};
    const ex::task_scheduler erased{concrete};
    const derived_eraser derived{concrete};

    // The eraser's own comparison would say equal. The scheduler's says no.
    EXPECT_FALSE(derived == concrete);
    EXPECT_EQ(own_comparisons, 1);
    EXPECT_FALSE(concrete == derived);
    EXPECT_EQ(own_comparisons, 2);
    EXPECT_TRUE(derived != concrete);
    EXPECT_EQ(own_comparisons, 3);
    EXPECT_FALSE(erased == concrete);
    EXPECT_FALSE(concrete == erased);
    EXPECT_EQ(own_comparisons, 5);

    // Without a competing overload the eraser's comparison is used.
    EXPECT_TRUE(erased == ex::task_scheduler{concrete});
    EXPECT_EQ(own_comparisons, 5);
}

// The hook itself: called by a hand-written awaiter, it completes the task
// with stopped and hands back a handle whose resumption does nothing.
struct stopping_awaiter {
    trace* log;

    bool await_ready() const noexcept { return false; }

    template<class Promise>
    std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> handle) const noexcept {
        static_assert(std::same_as<decltype(handle.promise().unhandled_stopped()), std::coroutine_handle<>>);
        static_assert(noexcept(std::declval<Promise&>().unhandled_stopped()));
        // *this lives in the frame the hook destroys.
        trace* const record = log;
        const std::coroutine_handle<> next = handle.promise().unhandled_stopped();
        record->add("hook-returned");
        // Two noop handles need not compare equal ([coroutine.noop.coroutine]),
        // so check the handle's state. Returning it below transfers to it, and
        // the events that follow show that nothing else was resumed.
        const bool noop = next && !next.done();
        record->add(noop ? "noop-handle" : "unexpected-handle");
        return next;
    }

    void await_resume() const noexcept {}
};

struct stopping_sender {
    using sender_concept = ex::sender_tag;

    template<class Self, class... Env>
    static consteval auto get_completion_signatures() noexcept {
        return ex::completion_signatures<ex::set_value_t(), ex::set_stopped_t()>{};
    }

    template<class Promise>
    auto as_awaitable(Promise&) const noexcept -> stopping_awaiter { return {log}; }

    trace* log;
};

ex::task<void, inline_environment> calls_stopped_hook(frame_marker, trace& log) {
    log.add("body");
    co_await stopping_sender{&log};
    log.add("after-await");
}

TEST(ExecutionTaskFrame, F06_UnhandledStoppedReturnsTheNoopHandle) {
    for (bool self_destroying : {false, true}) {
        SCOPED_TRACE(self_destroying);
        trace log;
        sink_base sink{log};
        operation_slot slot;
        slot.log = &log;
        auto& operation = slot.emplace(
            calls_stopped_hook(frame_marker{log}, log),
            sink_receiver{&sink, ex::env<>{}, self_destroying ? &slot : nullptr});
        ex::start(operation);
        events expected{"body", "frame-destroyed", "stopped"};
        if (self_destroying) expected.push_back("operation-destroyed");
        expected.insert(expected.end(), {"hook-returned", "noop-handle"});
        EXPECT_EQ(log.events(), expected);
    }
}

// F-07 replays the two worker schedules of the frame model in the real code:
// the terminal completes on a worker while the starting thread is still
// inside the child's start, and after the starting thread has returned.
TYPED_TEST(ChildTaskTest, F07_WorkerTerminalBeforeTheStartingThreadReturns) {
    for (auto what : {child_control::signal::stopped, child_control::signal::value}) {
        for (bool self_destroying : {false, true}) {
            SCOPED_TRACE(self_destroying);
            trace log;
            int_sink sink{log};
            auto control = std::make_shared<child_control>(
                child_control::mode::worker_before_return, what, log);
            operation_slot slot;
            slot.log = &log;
            auto& operation = slot.emplace(
                awaits_child<TypeParam>(frame_marker{log}, control),
                sink_receiver{&sink, child_receiver_env<TypeParam>(), self_destroying ? &slot : nullptr});
            ex::start(operation);
            // The starting thread came back through the child's start, the
            // await bridge and the task's start after everything was gone.
            events expected{"body", "child-start"};
            if (what == child_control::signal::stopped) {
                expected.insert(expected.end(), {"child-stopped", "child-destroyed", "frame-destroyed", "stopped"});
            } else {
                expected.insert(expected.end(),
                    {"child-value", "child-destroyed", "after-await", "frame-destroyed", "value"});
            }
            if (self_destroying) expected.push_back("operation-destroyed");
            expected.push_back("child-start-returns");
            EXPECT_EQ(log.events(), expected);
            if (what == child_control::signal::value) EXPECT_EQ(sink.received, 77);
        }
    }
}

TYPED_TEST(ChildTaskTest, F07_WorkerTerminalAfterTheStartingThreadReturned) {
    for (bool self_destroying : {false, true}) {
        SCOPED_TRACE(self_destroying);
        trace log;
        int_sink sink{log};
        auto control = std::make_shared<child_control>(
            child_control::mode::worker_after_return, child_control::signal::value, log);
        operation_slot slot;
        slot.log = &log;
        auto& operation = slot.emplace(
            awaits_child<TypeParam>(frame_marker{log}, control),
            sink_receiver{&sink, child_receiver_env<TypeParam>(), self_destroying ? &slot : nullptr});
        ex::start(operation);
        EXPECT_EQ(log.events(), (events{"body", "child-start", "child-start-returns"}));
        control->release();
        control->wait_for_terminal();
        events expected{"body", "child-start", "child-start-returns", "child-value", "child-destroyed",
                        "after-await", "frame-destroyed", "value"};
        if (self_destroying) expected.push_back("operation-destroyed");
        EXPECT_EQ(log.events(), expected);
        EXPECT_EQ(sink.received, 77);
    }
}

TYPED_TEST(ChildTaskTest, F07_InlineValueThroughTheChildCompletesInsideStart) {
    for (bool self_destroying : {false, true}) {
        SCOPED_TRACE(self_destroying);
        trace log;
        int_sink sink{log};
        auto control = std::make_shared<child_control>(
            child_control::mode::inline_completion, child_control::signal::value, log);
        operation_slot slot;
        slot.log = &log;
        auto& operation = slot.emplace(
            awaits_child<TypeParam>(frame_marker{log}, control),
            sink_receiver{&sink, child_receiver_env<TypeParam>(), self_destroying ? &slot : nullptr});
        ex::start(operation);
        // The await bridge defers the inline resumption until start returned.
        events expected{"body", "child-start", "child-value", "child-start-returns", "child-destroyed",
                        "after-await", "frame-destroyed", "value"};
        if (self_destroying) expected.push_back("operation-destroyed");
        EXPECT_EQ(log.events(), expected);
        EXPECT_EQ(sink.received, 77);
    }
}

} // namespace
