// Public integration of std::execution::task: [task.overview], [task.class],
// [task.members] and the await path of [task.promise]/6, through <execution>
// alone. Test names carry the pilot acceptance case they cover (H1-02 to
// H1-04) and, for the ten await operands, their label N1 to N10.

#include <execution>

#include <gtest/gtest.h>

#include <concepts>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <exception>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

namespace ex = std::execution;

// ---------------------------------------------------------------------------
// H1-01: the public surface, as constant checks. The configuration classes
// that must be rejected are configure probes in
// test/execution/compile/test_execution_task_configuration.cpp.

template<class Signatures, class Signature>
inline constexpr bool has_signature = false;
template<class... All, class Signature>
inline constexpr bool has_signature<ex::completion_signatures<All...>, Signature> =
    (std::same_as<All, Signature> || ...);

template<class Signatures>
inline constexpr std::size_t signature_count = 0;
template<class... All>
inline constexpr std::size_t signature_count<ex::completion_signatures<All...>> = sizeof...(All);

// [execution.syn], [task.class]: the defaults.
static_assert(std::same_as<ex::task<>, ex::task<void, ex::env<>>>);
static_assert(std::same_as<ex::task<int>, ex::task<int, ex::env<>>>);
static_assert(std::same_as<ex::task<>::sender_concept, ex::sender_tag>);
static_assert(std::same_as<ex::task<>::allocator_type, std::allocator<std::byte>>);
static_assert(std::same_as<ex::task<>::start_scheduler_type, ex::task_scheduler>);
static_assert(std::same_as<ex::task<>::stop_source_type, std::inplace_stop_source>);
static_assert(std::same_as<ex::task<>::stop_token_type, std::inplace_stop_token>);
static_assert(std::same_as<ex::task<>::error_types,
                           ex::completion_signatures<ex::set_error_t(std::exception_ptr)>>);

// [task.class]/2: each alias of the Environment is used when it names a type.
template<class T>
struct surface_allocator {
    using value_type = T;

    surface_allocator() = default;
    template<class U>
    surface_allocator(const surface_allocator<U>&) noexcept {}

    T* allocate(std::size_t count) { return std::allocator<T>{}.allocate(count); }
    void deallocate(T* pointer, std::size_t count) noexcept { std::allocator<T>{}.deallocate(pointer, count); }
    template<class U>
    bool operator==(const surface_allocator<U>&) const noexcept { return true; }
};

struct surface_token {
    template<class Callback>
    struct callback_type {
        template<class Init>
        callback_type(const surface_token& token, Init&& init) : inner(token.inner, std::forward<Init>(init)) {}

        std::inplace_stop_callback<Callback> inner;
    };

    bool stop_requested() const noexcept { return inner.stop_requested(); }
    bool stop_possible() const noexcept { return inner.stop_possible(); }
    bool operator==(const surface_token&) const noexcept = default;

    std::inplace_stop_token inner;
};

struct surface_source {
    surface_token get_token() const noexcept { return {inner.get_token()}; }
    bool stop_possible() const noexcept { return true; }
    bool stop_requested() const noexcept { return inner.stop_requested(); }
    bool request_stop() noexcept { return inner.request_stop(); }

    std::inplace_stop_source inner;
};

struct overriding_environment {
    using allocator_type = surface_allocator<std::byte>;
    using start_scheduler_type = ex::inline_scheduler;
    using stop_source_type = surface_source;
    using error_types = ex::completion_signatures<ex::set_error_t(int), ex::set_error_t(std::exception_ptr)>;
};

using overridden_task = ex::task<long, overriding_environment>;
static_assert(std::same_as<overridden_task::allocator_type, surface_allocator<std::byte>>);
static_assert(std::same_as<overridden_task::start_scheduler_type, ex::inline_scheduler>);
static_assert(std::same_as<overridden_task::stop_source_type, surface_source>);
// stop_token_type is deduced from the source.
static_assert(std::same_as<overridden_task::stop_token_type, surface_token>);
static_assert(std::same_as<overridden_task::error_types, overriding_environment::error_types>);

// One alias alone leaves the other three at their defaults.
struct one_alias_environment {
    using start_scheduler_type = ex::inline_scheduler;
};
static_assert(std::same_as<ex::task<int, one_alias_environment>::allocator_type, std::allocator<std::byte>>);
static_assert(std::same_as<ex::task<int, one_alias_environment>::stop_source_type, std::inplace_stop_source>);
static_assert(std::same_as<ex::task<int, one_alias_environment>::error_types,
                           ex::completion_signatures<ex::set_error_t(std::exception_ptr)>>);

struct allocator_alias_environment {
    using allocator_type = surface_allocator<std::byte>;
};
static_assert(std::same_as<ex::task<int, allocator_alias_environment>::allocator_type, surface_allocator<std::byte>>);
static_assert(std::same_as<ex::task<int, allocator_alias_environment>::start_scheduler_type, ex::task_scheduler>);
static_assert(std::same_as<ex::task<int, allocator_alias_environment>::stop_source_type, std::inplace_stop_source>);
static_assert(std::same_as<ex::task<int, allocator_alias_environment>::error_types,
                           ex::completion_signatures<ex::set_error_t(std::exception_ptr)>>);

struct stop_source_alias_environment {
    using stop_source_type = surface_source;
};
static_assert(std::same_as<ex::task<int, stop_source_alias_environment>::stop_source_type, surface_source>);
static_assert(std::same_as<ex::task<int, stop_source_alias_environment>::stop_token_type, surface_token>);
static_assert(std::same_as<ex::task<int, stop_source_alias_environment>::allocator_type, std::allocator<std::byte>>);
static_assert(std::same_as<ex::task<int, stop_source_alias_environment>::start_scheduler_type, ex::task_scheduler>);
static_assert(std::same_as<ex::task<int, stop_source_alias_environment>::error_types,
                           ex::completion_signatures<ex::set_error_t(std::exception_ptr)>>);

struct error_alias_environment {
    using error_types = ex::completion_signatures<ex::set_error_t(int)>;
};
static_assert(std::same_as<ex::task<int, error_alias_environment>::error_types,
                           ex::completion_signatures<ex::set_error_t(int)>>);
static_assert(std::same_as<ex::task<int, error_alias_environment>::allocator_type, std::allocator<std::byte>>);
static_assert(std::same_as<ex::task<int, error_alias_environment>::start_scheduler_type, ex::task_scheduler>);
static_assert(std::same_as<ex::task<int, error_alias_environment>::stop_source_type, std::inplace_stop_source>);

// [task.class]/1: the Environment is a class type, and a union is one.
union union_environment {
    using start_scheduler_type = ex::inline_scheduler;

    int unused;
};
static_assert(std::is_union_v<union_environment> && !std::is_class_v<union_environment>);
static_assert(sizeof(ex::task<int, union_environment>) != 0);
static_assert(std::same_as<ex::task<int, union_environment>::start_scheduler_type, ex::inline_scheduler>);
static_assert(std::same_as<ex::task<int, union_environment>::allocator_type, std::allocator<std::byte>>);
static_assert(signature_count<ex::completion_signatures_of_t<ex::task<int, union_environment>, ex::env<>>> == 3);

// [task.class]/1: void, a reference, or a cv-unqualified non-array object.
static_assert(sizeof(ex::task<void>) != 0 && sizeof(ex::task<int>) != 0);
static_assert(sizeof(ex::task<int&>) != 0 && sizeof(ex::task<const int&>) != 0 && sizeof(ex::task<int&&>) != 0);
static_assert(sizeof(ex::task<std::unique_ptr<int>>) != 0);

// [task.members]/3 and /4: the signature set. It has the value signature,
// each configured error and stopped, in no particular order and nothing else.
template<class Task>
using signatures_of = ex::completion_signatures_of_t<Task, ex::env<>>;

static_assert(ex::sender<ex::task<int>> && ex::sender_in<ex::task<int>, ex::env<>>);
static_assert(signature_count<signatures_of<ex::task<void>>> == 3);
static_assert(has_signature<signatures_of<ex::task<void>>, ex::set_value_t()>);
static_assert(has_signature<signatures_of<ex::task<void>>, ex::set_error_t(std::exception_ptr)>);
static_assert(has_signature<signatures_of<ex::task<void>>, ex::set_stopped_t()>);
static_assert(signature_count<signatures_of<ex::task<int>>> == 3);
static_assert(has_signature<signatures_of<ex::task<int>>, ex::set_value_t(int)>);
static_assert(has_signature<signatures_of<ex::task<int&>>, ex::set_value_t(int&)>);
static_assert(has_signature<signatures_of<ex::task<const int&>>, ex::set_value_t(const int&)>);
static_assert(has_signature<signatures_of<ex::task<int&&>>, ex::set_value_t(int&&)>);
static_assert(signature_count<signatures_of<overridden_task>> == 4);
static_assert(has_signature<signatures_of<overridden_task>, ex::set_value_t(long)>);
static_assert(has_signature<signatures_of<overridden_task>, ex::set_error_t(int)>);
static_assert(has_signature<signatures_of<overridden_task>, ex::set_error_t(std::exception_ptr)>);
static_assert(has_signature<signatures_of<overridden_task>, ex::set_stopped_t()>);
// An empty error list leaves the value and stopped.
struct no_error_environment {
    using start_scheduler_type = ex::inline_scheduler;
    using error_types = ex::completion_signatures<>;
};
static_assert(signature_count<signatures_of<ex::task<int, no_error_environment>>> == 2);
// The member returns an object of that type, in a constant expression.
static_assert(std::same_as<decltype(ex::task<int>::get_completion_signatures<ex::task<int>, ex::env<>>()),
                           signatures_of<ex::task<int>>>);
static_assert((ex::task<int>::get_completion_signatures<ex::task<int>, ex::env<>>(), true));

// [task.class]: move only, and connected as an rvalue only.
static_assert(std::is_nothrow_move_constructible_v<ex::task<int>>);
static_assert(!std::is_copy_constructible_v<ex::task<int>>);
static_assert(!std::is_copy_assignable_v<ex::task<int>> && !std::is_move_assignable_v<ex::task<int>>);

// [execution.syn]: with_error is an aggregate that stores remove_cvref_t<E>,
// and its guide takes the argument by value.
static_assert(std::is_aggregate_v<ex::with_error<int>>);
static_assert(std::same_as<ex::with_error<const int&>::type, int>);
static_assert(std::same_as<decltype(ex::with_error<const int&>::error), int>);
static_assert(std::same_as<decltype(ex::with_error{3}), ex::with_error<int>>);
static_assert(std::same_as<decltype(ex::with_error{std::declval<const long&>()}), ex::with_error<long>>);
static_assert(std::same_as<decltype(ex::with_error{"text"}), ex::with_error<const char*>>);

// ---------------------------------------------------------------------------
// [task.class]/4 with a scheduler that is defined in terms of the task it
// configures: its schedule sender owns a task whose start scheduler is that
// scheduler. The scheduler models scheduler only once the task is complete,
// so the task must not ask for that while it is being instantiated.

struct task_backed_scheduler;

struct task_backed_environment {
    using start_scheduler_type = task_backed_scheduler;
};

template<class Environment, class Scheduler>
struct task_backed_sender {
    using sender_concept = ex::sender_tag;

    template<class Self, class... Env>
    static consteval auto get_completion_signatures() noexcept {
        return ex::completion_signatures<ex::set_value_t(), ex::set_error_t(std::exception_ptr),
                                         ex::set_stopped_t()>{};
    }

    template<class Receiver>
    struct inner_receiver {
        using receiver_concept = ex::receiver_tag;

        struct env {
            Scheduler query(ex::get_start_scheduler_t) const noexcept { return scheduler; }

            Scheduler scheduler;
        };

        env get_env() const noexcept { return {scheduler}; }
        void set_value() && noexcept { ex::set_value(std::move(receiver)); }
        void set_error(std::exception_ptr error) && noexcept {
            ex::set_error(std::move(receiver), std::move(error));
        }
        void set_stopped() && noexcept { ex::set_stopped(std::move(receiver)); }

        Receiver receiver;
        Scheduler scheduler;
    };

    template<ex::receiver Receiver>
    auto connect(Receiver receiver) && {
        return ex::connect(std::move(owned), inner_receiver<Receiver>{std::move(receiver), scheduler});
    }

    // [exec.sched]: every advertised channel names the scheduler, as the
    // scheduler itself does.
    struct attributes {
        template<class Tag, class... Env>
        Scheduler query(ex::get_completion_scheduler_t<Tag>, const Env&...) const noexcept {
            return scheduler;
        }

        Scheduler scheduler;
    };
    attributes get_env() const noexcept { return {scheduler}; }

    ex::task<void, Environment> owned;
    Scheduler scheduler;
};

struct task_backed_scheduler {
    using scheduler_concept = ex::scheduler_tag;

    task_backed_sender<task_backed_environment, task_backed_scheduler> schedule() const;
    auto query(ex::get_forward_progress_guarantee_t) const noexcept {
        return ex::forward_progress_guarantee::weakly_parallel;
    }
    template<class Tag, class... Env>
    task_backed_scheduler query(ex::get_completion_scheduler_t<Tag>, const Env&...) const noexcept {
        return *this;
    }
    bool operator==(const task_backed_scheduler&) const noexcept = default;

    int* bodies;
};

// The first use that needs the task, the sender or the scheduler check.
static_assert(sizeof(ex::task<void, task_backed_environment>) != 0);
static_assert(ex::scheduler<task_backed_scheduler>);

inline ex::task<void, task_backed_environment> task_behind_the_scheduler(int* bodies) {
    ++*bodies;
    co_return;
}

inline task_backed_sender<task_backed_environment, task_backed_scheduler> task_backed_scheduler::schedule() const {
    return {task_behind_the_scheduler(bodies), *this};
}

// ---------------------------------------------------------------------------
// Execution agents. An agent is a queue of items that the test runs when it
// chooses to, on the test's own thread. Agent 0 is the caller: code that runs
// outside every queue.

struct agent {
    explicit agent(int id) : id(id) {}

    int id;
    std::deque<std::function<void()>> items;
};

agent* running_agent = nullptr;

int where() { return running_agent != nullptr ? running_agent->id : 0; }

// Runs the items that are queued now and those they queue, in order.
int drain(agent& target) {
    int ran = 0;
    while (!target.items.empty()) {
        auto item = std::move(target.items.front());
        target.items.pop_front();
        agent* const previous = std::exchange(running_agent, &target);
        item();
        running_agent = previous;
        ++ran;
    }
    return ran;
}

// A scheduler onto an agent. It cannot fail and cannot be stopped: its
// sender completes with set_value only. Kind makes Q and W different types.
template<int Kind>
struct agent_scheduler {
    using scheduler_concept = ex::scheduler_tag;

    template<class Receiver>
    struct operation {
        using operation_state_concept = ex::operation_state_tag;

        void start() & noexcept {
            target->items.push_back([this] { ex::set_value(std::move(receiver)); });
        }

        agent* target;
        Receiver receiver;
    };

    struct sender {
        using sender_concept = ex::sender_tag;

        template<class Self, class... Env>
        static consteval auto get_completion_signatures() noexcept {
            return ex::completion_signatures<ex::set_value_t()>{};
        }

        template<ex::receiver Receiver>
        auto connect(Receiver receiver) const -> operation<Receiver> { return {target, std::move(receiver)}; }

        // [exec.sched]: the value completion scheduler is the originating
        // scheduler.
        struct attributes {
            template<class... Env>
            agent_scheduler query(ex::get_completion_scheduler_t<ex::set_value_t>, const Env&...) const noexcept {
                return {target};
            }

            agent* target;
        };
        attributes get_env() const noexcept { return {target}; }

        agent* target;
    };

    sender schedule() const noexcept { return {target}; }
    auto query(ex::get_forward_progress_guarantee_t) const noexcept {
        return ex::forward_progress_guarantee::weakly_parallel;
    }
    // The same query on the scheduler, with and without an environment.
    template<class... Env>
    agent_scheduler query(ex::get_completion_scheduler_t<ex::set_value_t>, const Env&...) const noexcept {
        return *this;
    }
    bool operator==(const agent_scheduler&) const noexcept = default;

    agent* target;
};

// Q is the queued start scheduler of a task, W a separately identified worker.
using queued_scheduler = agent_scheduler<1>;
using worker_scheduler = agent_scheduler<2>;

static_assert(ex::scheduler<queued_scheduler> && ex::scheduler<worker_scheduler>);
static_assert(!std::same_as<queued_scheduler, worker_scheduler>);

// [exec.sched]: for a completion channel, the scheduler and the attributes of
// its schedule sender give the same answer, of the scheduler's type, without
// an environment and with one.
template<class Tag, class Source, class Scheduler>
concept names_scheduler = requires(const Source& source) {
    { ex::get_completion_scheduler<Tag>(source) } -> std::same_as<Scheduler>;
    { ex::get_completion_scheduler<Tag>(source, ex::env<>{}) } -> std::same_as<Scheduler>;
};

template<class Tag, class Scheduler>
concept agrees_on =
    names_scheduler<Tag, Scheduler, std::remove_cv_t<Scheduler>> &&
    names_scheduler<Tag, decltype(ex::get_env(ex::schedule(std::declval<Scheduler&>()))), std::remove_cv_t<Scheduler>>;

template<class Tag, class Scheduler>
bool agrees_on_value_of(Scheduler& scheduler) {
    const auto attributes = ex::get_env(ex::schedule(scheduler));
    const std::remove_cv_t<Scheduler> expected = scheduler;
    return ex::get_completion_scheduler<Tag>(scheduler) == expected &&
           ex::get_completion_scheduler<Tag>(scheduler, ex::env<>{}) == expected &&
           ex::get_completion_scheduler<Tag>(attributes) == expected &&
           ex::get_completion_scheduler<Tag>(attributes, ex::env<>{}) == expected;
}

static_assert(agrees_on<ex::set_value_t, queued_scheduler> && agrees_on<ex::set_value_t, worker_scheduler>);
static_assert(agrees_on<ex::set_value_t, task_backed_scheduler> && agrees_on<ex::set_error_t, task_backed_scheduler> &&
              agrees_on<ex::set_stopped_t, task_backed_scheduler>);

TEST(ExecutionTaskIntegration, FixtureSchedulersNameThemselvesInTheirScheduleSenders) {
    agent first{1}, second{2};
    queued_scheduler queued{&first}, other_queued{&second};
    worker_scheduler worker{&second};
    EXPECT_TRUE(agrees_on_value_of<ex::set_value_t>(queued));
    EXPECT_TRUE(agrees_on_value_of<ex::set_value_t>(worker));
    EXPECT_FALSE(ex::get_completion_scheduler<ex::set_value_t>(ex::get_env(ex::schedule(queued))) == other_queued);
    int bodies = 0;
    task_backed_scheduler backed{&bodies};
    EXPECT_TRUE(agrees_on_value_of<ex::set_value_t>(backed));
    EXPECT_TRUE(agrees_on_value_of<ex::set_error_t>(backed));
    EXPECT_TRUE(agrees_on_value_of<ex::set_stopped_t>(backed));
    EXPECT_EQ(bodies, 0);
}

// The two task environments of the await table: I is exactly
// inline_scheduler, Q the queued scheduler type.
struct inline_environment {
    using start_scheduler_type = ex::inline_scheduler;
};

struct queued_environment {
    using start_scheduler_type = queued_scheduler;
};

// ---------------------------------------------------------------------------
// The receiver. It accepts whatever the task's signatures deliver, counts
// terminals and records the agent that delivered.

struct outcome {
    int terminals = 0;
    std::string channel;
    std::optional<int> value;
    std::exception_ptr exception;
    std::optional<int> typed_error;
    int agent = -1;
    int frames_at_terminal = -1;

    void entered(const char* which);
};

// Frames of the tasks under test that are alive. A coroutine takes one
// marker by value. Only the copy that ends up in the frame is still armed
// when it dies, so the count falls when the frame is destroyed and not when
// the body is left.
int live_frames = 0;

using events = std::vector<std::string>;

struct frame_marker {
    frame_marker(events* log = nullptr) : log(log), armed(true) { ++live_frames; }
    frame_marker(frame_marker&& other) noexcept : log(other.log), armed(std::exchange(other.armed, false)) {}
    ~frame_marker() {
        if (!armed) return;
        --live_frames;
        if (log) log->push_back("frame destroyed");
    }

    events* log;
    bool armed;
};

void outcome::entered(const char* which) {
    ++terminals;
    channel = which;
    agent = where();
    frames_at_terminal = live_frames;
}

template<class Scheduler>
struct start_env {
    Scheduler query(ex::get_start_scheduler_t) const noexcept { return scheduler; }

    Scheduler scheduler;
};

template<class Scheduler>
struct probe_receiver {
    using receiver_concept = ex::receiver_tag;

    start_env<Scheduler> get_env() const noexcept { return {scheduler}; }

    void set_value() && noexcept { out->entered("value"); }
    void set_value(int value) && noexcept {
        out->value = value;
        out->entered("value");
    }
    void set_error(std::exception_ptr error) && noexcept {
        out->exception = std::move(error);
        out->entered("exception");
    }
    void set_error(int error) && noexcept {
        out->typed_error = error;
        out->entered("typed");
    }
    void set_stopped() && noexcept { out->entered("stopped"); }

    outcome* out;
    Scheduler scheduler;
};

// One task environment with its start scheduler and the agent that starts it.
struct inline_variant {
    using environment = inline_environment;

    ex::inline_scheduler scheduler() const noexcept { return {}; }
    int home() const noexcept { return 0; }

    // Connects and starts on the caller, and runs every agent to quiescence.
    template<class Task>
    outcome run(Task task) {
        outcome done;
        {
            auto operation = ex::connect(std::move(task), probe_receiver<ex::inline_scheduler>{&done, {}});
            ex::start(operation);
            settle();
            // The terminal arrived while the operation is alive, not from its
            // destruction.
            EXPECT_EQ(done.terminals, 1);
        }
        return done;
    }

    void settle() {
        while (drain(queue) + drain(worker) != 0) {}
    }

    agent queue{1};
    agent worker{2};
};

struct queued_variant {
    using environment = queued_environment;

    queued_scheduler scheduler() noexcept { return {&queue}; }
    int home() const noexcept { return 1; }

    // Connects on the caller and starts from the queue's own agent, which is
    // the agent of the advertised start scheduler.
    template<class Task>
    outcome run(Task task) {
        outcome done;
        {
            auto operation =
                ex::connect(std::move(task), probe_receiver<queued_scheduler>{&done, queued_scheduler{&queue}});
            queue.items.push_back([&operation] { ex::start(operation); });
            settle();
            EXPECT_EQ(done.terminals, 1);
        }
        return done;
    }

    void settle() {
        while (drain(queue) + drain(worker) != 0) {}
    }

    agent queue{1};
    agent worker{2};
};

template<class Variant>
class ExecutionTaskAwait : public ::testing::Test {
protected:
    Variant variant;
};

using both_variants = ::testing::Types<inline_variant, queued_variant>;
TYPED_TEST_SUITE(ExecutionTaskAwait, both_variants);

template<class Environment>
using int_task = ex::task<int, Environment>;

// ---------------------------------------------------------------------------
// H1-03: the ten one-level await operands

// N1: just()
template<class Environment>
int_task<Environment> awaits_just(events* log, frame_marker = {}) {
    log->push_back("begin@" + std::to_string(where()));
    co_await ex::just();
    log->push_back("resumed@" + std::to_string(where()));
    co_return 17;
}

TYPED_TEST(ExecutionTaskAwait, H103_N1_Just) {
    events log;
    const auto home = std::to_string(this->variant.home());
    const outcome done = this->variant.run(awaits_just<typename TypeParam::environment>(&log));
    // Resumed once, on the starting agent, and then the sentinel.
    EXPECT_EQ(log, (events{"begin@" + home, "resumed@" + home}));
    EXPECT_EQ(done.terminals, 1);
    EXPECT_EQ(done.channel, "value");
    EXPECT_EQ(done.value, 17);
    EXPECT_EQ(done.frames_at_terminal, 0);
}

// N2: just(7)
template<class Environment>
int_task<Environment> awaits_just_value(events* log, frame_marker = {}) {
    const int value = co_await ex::just(7);
    log->push_back("resumed@" + std::to_string(where()));
    co_return value;
}

TYPED_TEST(ExecutionTaskAwait, H103_N2_JustValue) {
    events log;
    const outcome done = this->variant.run(awaits_just_value<typename TypeParam::environment>(&log));
    EXPECT_EQ(log, (events{"resumed@" + std::to_string(this->variant.home())}));
    EXPECT_EQ(done.terminals, 1);
    EXPECT_EQ(done.value, 7);
    EXPECT_EQ(done.frames_at_terminal, 0);
}

// N3: then(just(3), twice)
template<class Environment>
int_task<Environment> awaits_then(int* calls, frame_marker = {}) {
    co_return co_await ex::then(ex::just(3), [calls](int value) {
        ++*calls;
        return 2 * value;
    });
}

TYPED_TEST(ExecutionTaskAwait, H103_N3_Then) {
    int calls = 0;
    const outcome done = this->variant.run(awaits_then<typename TypeParam::environment>(&calls));
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(done.terminals, 1);
    EXPECT_EQ(done.value, 6);
    EXPECT_EQ(done.agent, this->variant.home());
}

// N4: just_error(ep)
struct marked_error : std::runtime_error {
    explicit marked_error(int mark) : std::runtime_error("marked_error"), mark(mark) {}
    int mark;
};

int mark_of(const std::exception_ptr& error) {
    if (!error) return -1;
    try {
        std::rethrow_exception(error);
    } catch (const marked_error& marked) {
        return marked.mark;
    } catch (...) {
        return -2;
    }
}

template<class Environment>
int_task<Environment> awaits_just_error(bool catches, events* log, frame_marker = {}) {
    auto stored = std::make_exception_ptr(marked_error{41});
    if (catches) {
        try {
            co_await ex::just_error(stored);
            log->push_back("not thrown");
        } catch (const marked_error& marked) {
            log->push_back("caught@" + std::to_string(where()));
            co_return marked.mark;
        }
    }
    co_await ex::just_error(std::move(stored));
    log->push_back("unreachable");
    co_return 0;
}

TYPED_TEST(ExecutionTaskAwait, H103_N4_JustError) {
    {
        // The await throws the stored exception into the body.
        events log;
        const outcome done = this->variant.run(awaits_just_error<typename TypeParam::environment>(true, &log));
        EXPECT_EQ(log, (events{"caught@" + std::to_string(this->variant.home())}));
        EXPECT_EQ(done.channel, "value");
        EXPECT_EQ(done.value, 41);
    }
    {
        // Uncaught, it reaches the outer exception channel.
        events log;
        const outcome done = this->variant.run(awaits_just_error<typename TypeParam::environment>(false, &log));
        EXPECT_TRUE(log.empty());
        EXPECT_EQ(done.terminals, 1);
        EXPECT_EQ(done.channel, "exception");
        EXPECT_EQ(mark_of(done.exception), 41);
        EXPECT_EQ(done.frames_at_terminal, 0);
    }
}

// N5: just_stopped()
template<class Environment>
int_task<Environment> awaits_just_stopped(events* log, frame_marker = {}) {
    log->push_back("before");
    co_await ex::just_stopped();
    log->push_back("after");
    co_return 0;
}

TYPED_TEST(ExecutionTaskAwait, H103_N5_JustStopped) {
    events log;
    const outcome done = this->variant.run(awaits_just_stopped<typename TypeParam::environment>(&log));
    // Stopped once, and no statement after the await ran.
    EXPECT_EQ(log, (events{"before"}));
    EXPECT_EQ(done.terminals, 1);
    EXPECT_EQ(done.channel, "stopped");
    EXPECT_EQ(done.frames_at_terminal, 0);
}

// N6: read_env(get_start_scheduler)
template<class Environment, class Scheduler>
int_task<Environment> awaits_start_scheduler(Scheduler expected, events* log, frame_marker = {}) {
    auto current = co_await ex::read_env(ex::get_start_scheduler);
    static_assert(std::same_as<decltype(current), Scheduler>);
    log->push_back("resumed@" + std::to_string(where()));
    co_return current == expected ? 1 : 0;
}

TYPED_TEST(ExecutionTaskAwait, H103_N6_ReadStartScheduler) {
    events log;
    const outcome done = this->variant.run(
        awaits_start_scheduler<typename TypeParam::environment>(this->variant.scheduler(), &log));
    EXPECT_EQ(log, (events{"resumed@" + std::to_string(this->variant.home())}));
    EXPECT_EQ(done.terminals, 1);
    EXPECT_EQ(done.value, 1);
}

// N9: child_task(11)
template<class Environment>
int_task<Environment> child_task(int value, frame_marker = {}) {
    co_return value;
}

template<class Environment>
int_task<Environment> awaits_child_task(events* log, frame_marker = {}) {
    const int value = co_await child_task<Environment>(11);
    // Only this frame is left: the child's is gone before the value is seen.
    log->push_back("frames=" + std::to_string(live_frames) + "@" + std::to_string(where()));
    co_return value;
}

TYPED_TEST(ExecutionTaskAwait, H103_N9_ChildTask) {
    events log;
    const outcome done = this->variant.run(awaits_child_task<typename TypeParam::environment>(&log));
    EXPECT_EQ(log, (events{"frames=1@" + std::to_string(this->variant.home())}));
    EXPECT_EQ(done.terminals, 1);
    EXPECT_EQ(done.value, 11);
    EXPECT_EQ(done.frames_at_terminal, 0);
}

// N10: a one-value sender with a member affine()
struct affine_log {
    int member_affine = 0, original_connects = 0, affine_connects = 0;
};

// What member affine() returns: a sender that completes where it was
// started, which here is the start resource of the awaiting task.
struct affinity_preserving_sender {
    using sender_concept = ex::sender_tag;

    template<class Self, class... Env>
    static consteval auto get_completion_signatures() noexcept {
        return ex::completion_signatures<ex::set_value_t(int)>{};
    }

    template<class Receiver>
    struct operation {
        using operation_state_concept = ex::operation_state_tag;

        // set_value_t(int): the value is delivered as an rvalue.
        void start() & noexcept { ex::set_value(std::move(receiver), std::move(value)); }

        Receiver receiver;
        int value;
    };

    template<ex::receiver Receiver>
    auto connect(Receiver receiver) const -> operation<Receiver> {
        ++log->affine_connects;
        return {std::move(receiver), value};
    }

    affine_log* log;
    int value;
};

struct self_affine_sender {
    using sender_concept = ex::sender_tag;

    template<class Self, class... Env>
    static consteval auto get_completion_signatures() noexcept {
        return ex::completion_signatures<ex::set_value_t(int)>{};
    }

    template<ex::receiver Receiver>
    auto connect(Receiver receiver) const -> affinity_preserving_sender::operation<Receiver> {
        ++log->original_connects;
        return {std::move(receiver), value};
    }

    affinity_preserving_sender affine() const noexcept {
        ++log->member_affine;
        return {log, value};
    }

    affine_log* log;
    int value;
};

// A receiver that tells the value category it is given.
struct category_receiver {
    using receiver_concept = ex::receiver_tag;

    void set_value(int& value) && noexcept { *seen = "lvalue " + std::to_string(value); }
    void set_value(int&& value) && noexcept { *seen = "rvalue " + std::to_string(value); }

    std::string* seen;
};

template<class Environment>
int_task<Environment> awaits_self_affine(affine_log* selections, events* log, frame_marker = {}) {
    const int value = co_await self_affine_sender{selections, 13};
    log->push_back("resumed@" + std::to_string(where()));
    co_return value;
}

TYPED_TEST(ExecutionTaskAwait, H103_N10_MemberAffine) {
    {
        // Both senders deliver what they advertise, set_value_t(int): an
        // rvalue.
        affine_log direct;
        std::string original, affine;
        auto first = ex::connect(self_affine_sender{&direct, 13}, category_receiver{&original});
        ex::start(first);
        auto second = ex::connect(self_affine_sender{&direct, 13}.affine(), category_receiver{&affine});
        ex::start(second);
        EXPECT_EQ(original, "rvalue 13");
        EXPECT_EQ(affine, "rvalue 13");
    }
    affine_log selections;
    events log;
    const outcome done =
        this->variant.run(awaits_self_affine<typename TypeParam::environment>(&selections, &log));
    EXPECT_EQ(log, (events{"resumed@" + std::to_string(this->variant.home())}));
    EXPECT_EQ(done.terminals, 1);
    EXPECT_EQ(done.value, 13);
    if (std::same_as<typename TypeParam::environment, inline_environment>) {
        // I awaits the original sender directly.
        EXPECT_EQ(selections.member_affine, 0);
        EXPECT_EQ(selections.original_connects, 1);
        EXPECT_EQ(selections.affine_connects, 0);
    } else {
        // Q selects the member affine() once, and connects what it returned.
        EXPECT_EQ(selections.member_affine, 1);
        EXPECT_EQ(selections.original_connects, 0);
        EXPECT_EQ(selections.affine_connects, 1);
    }
}

// N7: continues_on(just(9), W), under Q only
int_task<queued_environment> awaits_worker(worker_scheduler worker, events* log, frame_marker = {}) {
    log->push_back("begin@" + std::to_string(where()));
    const int value = co_await ex::continues_on(ex::just(9), worker);
    log->push_back("resumed@" + std::to_string(where()));
    co_return value;
}

TEST(ExecutionTaskAwaitQueued, H103_N7_ContinuesOnWorker) {
    agent queue{1}, worker{2};
    events log;
    outcome done;
    {
        auto operation = ex::connect(awaits_worker(worker_scheduler{&worker}, &log),
                                     probe_receiver<queued_scheduler>{&done, queued_scheduler{&queue}});
        queue.items.push_back([&operation] { ex::start(operation); });
        // Q starts the task. The child moves to W, which has not run yet.
        EXPECT_EQ(drain(queue), 1);
        EXPECT_EQ(log, (events{"begin@1"}));
        EXPECT_EQ(worker.items.size(), 1u);
        EXPECT_TRUE(queue.items.empty());
        // W delivers the child's value. The task has not continued: its
        // continuation is queued on Q.
        EXPECT_EQ(drain(worker), 1);
        EXPECT_EQ(log, (events{"begin@1"}));
        EXPECT_EQ(done.terminals, 0);
        EXPECT_EQ(queue.items.size(), 1u);
        // Q runs the continuation, and the terminal arrives before the
        // operation is destroyed.
        EXPECT_EQ(drain(queue), 1);
        EXPECT_EQ(done.terminals, 1);
        EXPECT_EQ(log, (events{"begin@1", "resumed@1"}));
    }
    EXPECT_EQ(log, (events{"begin@1", "resumed@1"}));
    EXPECT_EQ(done.terminals, 1);
    EXPECT_EQ(done.value, 9);
    EXPECT_EQ(done.agent, 1);
    EXPECT_EQ(done.frames_at_terminal, 0);
    EXPECT_TRUE(worker.items.empty());
}

// N8: a named, copyable lvalue sender whose connect depends on its category,
// under I only. LWG 4547: the awaited lvalue is connected as an lvalue, and
// it is the named object itself.
struct category_log {
    int copies = 0, moves = 0, lvalue_connects = 0, rvalue_connects = 0;
    const void* connected = nullptr;
};

struct category_sender {
    using sender_concept = ex::sender_tag;

    category_sender(category_log& log, int value) noexcept : log(&log), value(value) {}
    category_sender(const category_sender& other) noexcept : log(other.log), value(other.value) {
        ++log->copies;
    }
    category_sender(category_sender&& other) noexcept : log(other.log), value(std::exchange(other.value, -1)) {
        ++log->moves;
    }

    // One value signature for either category, of different types.
    template<class Self, class... Env>
    static consteval auto get_completion_signatures() noexcept
        -> std::conditional_t<std::is_lvalue_reference_v<Self>, ex::completion_signatures<ex::set_value_t(int)>,
                              ex::completion_signatures<ex::set_value_t(long)>> {
        return {};
    }

    template<class Receiver, class Value>
    struct operation {
        using operation_state_concept = ex::operation_state_tag;

        void start() & noexcept { ex::set_value(std::move(receiver), std::move(value)); }

        Receiver receiver;
        Value value;
    };

    template<ex::receiver Receiver>
    auto connect(Receiver receiver) & noexcept -> operation<Receiver, int> {
        ++log->lvalue_connects;
        log->connected = this;
        return {std::move(receiver), value};
    }
    template<ex::receiver Receiver>
    auto connect(Receiver receiver) && noexcept -> operation<Receiver, long> {
        ++log->rvalue_connects;
        log->connected = this;
        return {std::move(receiver), value};
    }

    category_log* log;
    int value;
};

int_task<inline_environment> awaits_named_lvalue(category_log* log, bool* connected_the_named_object,
                                                 frame_marker = {}) {
    category_sender source{*log, 23};
    decltype(auto) value = co_await source;
    // Compared here, while the named sender is alive.
    *connected_the_named_object = log->connected == std::addressof(source);
    // The lvalue connection's value type.
    static_assert(std::same_as<decltype(value), int>);
    co_return value;
}

TEST(ExecutionTaskAwaitInline, H103_N8_NamedLvalueSender) {
    category_log log;
    bool connected_the_named_object = false;
    inline_variant variant;
    const outcome done = variant.run(awaits_named_lvalue(&log, &connected_the_named_object));
    EXPECT_EQ(done.terminals, 1);
    EXPECT_EQ(done.value, 23);
    EXPECT_EQ(log.lvalue_connects, 1);
    EXPECT_EQ(log.rvalue_connects, 0);
    EXPECT_EQ(log.copies, 0);
    EXPECT_EQ(log.moves, 0);
    EXPECT_TRUE(connected_the_named_object);
}

// ---------------------------------------------------------------------------
// H1-01 at run time: the overriding environment is usable, and its aliases
// are what the task hands out.

template<class Receiver>
concept connects_as_lvalue = requires(ex::task<int, inline_environment>& task, Receiver receiver) {
    ex::connect(task, std::move(receiver));
};
static_assert(!connects_as_lvalue<probe_receiver<ex::inline_scheduler>>);

// The member itself: connect is callable on an rvalue task only. The connect
// customization point never reaches the member for an lvalue, because a task
// cannot be copied.
template<class Task, class Receiver>
concept member_connects_lvalue = requires(Task& task, Receiver receiver) { task.connect(std::move(receiver)); };
template<class Task, class Receiver>
concept member_connects_rvalue =
    requires(Task& task, Receiver receiver) { std::move(task).connect(std::move(receiver)); };
static_assert(member_connects_rvalue<ex::task<int, inline_environment>, probe_receiver<ex::inline_scheduler>>);
static_assert(!member_connects_lvalue<ex::task<int, inline_environment>, probe_receiver<ex::inline_scheduler>>);
static_assert(member_connects_rvalue<ex::task<void>, probe_receiver<ex::task_scheduler>>);
static_assert(!member_connects_lvalue<ex::task<void>, probe_receiver<ex::task_scheduler>>);

overridden_task uses_every_alias(bool fails) {
    auto scheduler = co_await ex::read_env(ex::get_start_scheduler);
    static_assert(std::same_as<decltype(scheduler), ex::inline_scheduler>);
    if (fails) co_yield ex::with_error{5};
    co_return 81L;
}

TEST(ExecutionTaskIntegration, H101_OverridingEnvironmentIsUsedThroughout) {
    struct long_receiver {
        using receiver_concept = ex::receiver_tag;

        start_env<ex::inline_scheduler> get_env() const noexcept { return {}; }
        void set_value(long value) && noexcept {
            out->value = static_cast<int>(value);
            out->entered("value");
        }
        void set_error(std::exception_ptr) && noexcept { out->entered("exception"); }
        void set_error(int error) && noexcept {
            out->typed_error = error;
            out->entered("typed");
        }
        void set_stopped() && noexcept { out->entered("stopped"); }

        outcome* out;
    };
    {
        outcome done;
        auto operation = ex::connect(uses_every_alias(false), long_receiver{&done});
        ex::start(operation);
        EXPECT_EQ(done.terminals, 1);
        EXPECT_EQ(done.value, 81);
    }
    {
        outcome done;
        auto operation = ex::connect(uses_every_alias(true), long_receiver{&done});
        ex::start(operation);
        EXPECT_EQ(done.terminals, 1);
        EXPECT_EQ(done.channel, "typed");
        EXPECT_EQ(done.typed_error, 5);
    }
    // The aggregate and its guide.
    const ex::with_error<int> braced{7};
    const ex::with_error deduced{braced.error};
    EXPECT_EQ(deduced.error, 7);
}

TEST(ExecutionTaskIntegration, H101_UnionEnvironmentIsAClassType) {
    live_frames = 0;
    events log;
    inline_variant variant;
    const outcome done = variant.run(awaits_just_value<union_environment>(&log));
    EXPECT_EQ(done.terminals, 1);
    EXPECT_EQ(done.value, 7);
    EXPECT_EQ(done.frames_at_terminal, 0);
}

TEST(ExecutionTaskIntegration, H101_TaskBackedSchedulerIsAStartScheduler) {
    struct counting_receiver {
        using receiver_concept = ex::receiver_tag;

        void set_value() && noexcept { ++*values; }
        void set_error(std::exception_ptr) && noexcept { ++*others; }
        void set_stopped() && noexcept { ++*others; }

        int* values;
        int* others;
    };
    int bodies = 0, values = 0, others = 0;
    {
        auto operation =
            ex::connect(ex::schedule(task_backed_scheduler{&bodies}), counting_receiver{&values, &others});
        EXPECT_EQ(bodies, 0);
        ex::start(operation);
        EXPECT_EQ(values, 1);
    }
    EXPECT_EQ(bodies, 1);
    EXPECT_EQ(values, 1);
    EXPECT_EQ(others, 0);
}

// A cv-qualified start_scheduler_type on the await path. The scheduler is
// usable through a volatile object, and it is not the inline scheduler, so
// every await goes through affine. Senders whose value results are ordinary
// objects are awaited as under any other scheduler. Awaiting
// read_env(get_start_scheduler) is not supported for a volatile alias: its
// value result has the volatile type, which the scheduler-affine path cannot
// carry. That limit is recorded in the conformance ledger.
struct volatile_usable_scheduler {
    using scheduler_concept = ex::scheduler_tag;

    volatile_usable_scheduler() = default;
    volatile_usable_scheduler(const volatile_usable_scheduler&) noexcept = default;
    volatile_usable_scheduler(const volatile volatile_usable_scheduler&) noexcept {}
    volatile_usable_scheduler& operator=(const volatile_usable_scheduler&) noexcept = default;

    // Completes inline, and names this scheduler type in its attributes.
    struct sender {
        using sender_concept = ex::sender_tag;

        template<class Self, class... Env>
        static consteval auto get_completion_signatures() noexcept {
            return ex::completion_signatures<ex::set_value_t()>{};
        }

        template<class Receiver>
        struct operation {
            using operation_state_concept = ex::operation_state_tag;

            void start() & noexcept { ex::set_value(std::move(receiver)); }

            Receiver receiver;
        };

        template<ex::receiver Receiver>
        auto connect(Receiver receiver) const -> operation<Receiver> { return {std::move(receiver)}; }

        struct attributes {
            template<class... Env>
            volatile_usable_scheduler query(ex::get_completion_scheduler_t<ex::set_value_t>,
                                            const Env&...) const noexcept {
                return {};
            }
        };
        attributes get_env() const noexcept { return {}; }
    };

    sender schedule() const volatile noexcept { return {}; }
    template<class... Env>
    volatile_usable_scheduler query(ex::get_completion_scheduler_t<ex::set_value_t>,
                                    const Env&...) const volatile noexcept {
        return {};
    }
    auto query(ex::get_forward_progress_guarantee_t) const volatile noexcept {
        return ex::forward_progress_guarantee::weakly_parallel;
    }
    friend bool operator==(const volatile volatile_usable_scheduler&,
                           const volatile volatile_usable_scheduler&) noexcept {
        return true;
    }
};

struct volatile_alias_environment {
    using start_scheduler_type = volatile volatile_usable_scheduler;
};

static_assert(ex::scheduler<volatile volatile_usable_scheduler>);
// The scheduler and its schedule sender name the same scheduler type.
static_assert(agrees_on<ex::set_value_t, volatile volatile_usable_scheduler>);
static_assert(agrees_on<ex::set_value_t, volatile_usable_scheduler>);

TEST(ExecutionTaskIntegration, H103_VolatileStartSchedulerAliasAwaitsOrdinaryValues) {
    live_frames = 0;
    outcome done;
    events log;
    {
        auto operation = ex::connect(awaits_just_value<volatile_alias_environment>(&log),
                                     probe_receiver<volatile_usable_scheduler>{&done, {}});
        ex::start(operation);
        EXPECT_EQ(done.terminals, 1);
    }
    EXPECT_EQ(done.terminals, 1);
    EXPECT_EQ(done.value, 7);
    EXPECT_EQ(log.size(), 1u);
    {
        outcome stopped;
        events stopped_log;
        auto operation = ex::connect(awaits_just_stopped<volatile_alias_environment>(&stopped_log),
                                     probe_receiver<volatile_usable_scheduler>{&stopped, {}});
        ex::start(operation);
        EXPECT_EQ(stopped.channel, "stopped");
        EXPECT_EQ(stopped_log, (events{"before"}));
    }
    EXPECT_EQ(live_frames, 0);
}

// ---------------------------------------------------------------------------
// H1-02: connect and start, every terminal, frame release

struct typed_environment_base {
    using error_types = ex::completion_signatures<ex::set_error_t(std::exception_ptr), ex::set_error_t(int)>;
};

struct inline_typed_environment : typed_environment_base {
    using start_scheduler_type = ex::inline_scheduler;
};

struct queued_typed_environment : typed_environment_base {
    using start_scheduler_type = queued_scheduler;
};

enum class ending { value, exception, typed_error, stopped };

template<class Environment>
ex::task<int, Environment> ends_with(ending how, events* log, frame_marker = {}) {
    log->push_back("body@" + std::to_string(where()));
    if (how == ending::exception) throw marked_error{52};
    if (how == ending::typed_error) co_yield ex::with_error{53};
    if (how == ending::stopped) co_await ex::just_stopped();
    co_return 51;
}

template<class Environment>
ex::task<void, Environment> ends_void(events* log, frame_marker = {}) {
    log->push_back("body@" + std::to_string(where()));
    co_return;
}

template<class Variant>
struct typed_environment_of;
template<>
struct typed_environment_of<inline_variant> {
    using type = inline_typed_environment;
};
template<>
struct typed_environment_of<queued_variant> {
    using type = queued_typed_environment;
};

TYPED_TEST(ExecutionTaskAwait, H102_ConnectAndStartDeliverEachTerminalOnce) {
    using environment = typename typed_environment_of<TypeParam>::type;
    const auto home = std::to_string(this->variant.home());
    live_frames = 0;
    {
        events log;
        const outcome done = this->variant.run(ends_void<environment>(&log));
        EXPECT_EQ(log, (events{"body@" + home}));
        EXPECT_EQ(done.terminals, 1);
        EXPECT_EQ(done.channel, "value");
        EXPECT_FALSE(done.value);
        EXPECT_EQ(done.frames_at_terminal, 0);
    }
    {
        events log;
        const outcome done = this->variant.run(ends_with<environment>(ending::value, &log));
        EXPECT_EQ(done.terminals, 1);
        EXPECT_EQ(done.channel, "value");
        EXPECT_EQ(done.value, 51);
        EXPECT_EQ(done.agent, this->variant.home());
        EXPECT_EQ(done.frames_at_terminal, 0);
    }
    {
        events log;
        const outcome done = this->variant.run(ends_with<environment>(ending::exception, &log));
        EXPECT_EQ(done.terminals, 1);
        EXPECT_EQ(done.channel, "exception");
        EXPECT_EQ(mark_of(done.exception), 52);
        EXPECT_EQ(done.frames_at_terminal, 0);
    }
    {
        events log;
        const outcome done = this->variant.run(ends_with<environment>(ending::typed_error, &log));
        EXPECT_EQ(done.terminals, 1);
        EXPECT_EQ(done.channel, "typed");
        EXPECT_EQ(done.typed_error, 53);
        EXPECT_EQ(done.frames_at_terminal, 0);
    }
    {
        events log;
        const outcome done = this->variant.run(ends_with<environment>(ending::stopped, &log));
        EXPECT_EQ(done.terminals, 1);
        EXPECT_EQ(done.channel, "stopped");
        EXPECT_EQ(done.frames_at_terminal, 0);
    }
    EXPECT_EQ(live_frames, 0);
}

// The default task: env<>, task_scheduler as its start scheduler, run by
// sync_wait on its run loop.
ex::task<int> default_value_task(frame_marker = {}) {
    const int value = co_await ex::just(61);
    co_return value;
}

ex::task<> default_void_task(int* ran, frame_marker = {}) {
    co_await ex::just();
    ++*ran;
}

ex::task<int> default_throwing_task(frame_marker = {}) {
    co_await ex::just();
    throw marked_error{62};
}

ex::task<int> default_stopped_task(frame_marker = {}) {
    co_await ex::just_stopped();
    co_return 0;
}

// 1: the task's start scheduler is the one handed in. 2: it is an erased
// inline scheduler.
ex::task<int> default_compares_start_scheduler(ex::task_scheduler expected, frame_marker = {}) {
    auto current = co_await ex::read_env(ex::get_start_scheduler);
    static_assert(std::same_as<decltype(current), ex::task_scheduler>);
    co_return (current == expected ? 1 : 0) + (current == ex::task_scheduler{ex::inline_scheduler{}} ? 2 : 0);
}

TEST(ExecutionTaskIntegration, H102_DefaultTaskStartsOnTheWaitingRunLoop) {
    live_frames = 0;
    // The start scheduler of the wait's own environment, erased and handed to
    // a default task, is what that task finds as its start scheduler. An
    // inline scheduler in its place would compare unequal.
    auto result = std::this_thread::sync_wait(
        ex::let_value(ex::read_env(ex::get_start_scheduler), [](auto outer) {
            static_assert(!std::same_as<decltype(outer), ex::inline_scheduler>);
            return default_compares_start_scheduler(ex::task_scheduler{outer});
        }));
    ASSERT_TRUE(result);
    EXPECT_EQ(std::get<0>(*result), 1);
    {
        // The control: the same task started with an inline start scheduler
        // finds the erased inline scheduler, and not a run loop's.
        ex::run_loop loop;
        outcome done;
        auto operation = ex::connect(default_compares_start_scheduler(ex::task_scheduler{loop.get_scheduler()}),
                                     probe_receiver<ex::inline_scheduler>{&done, {}});
        ex::start(operation);
        EXPECT_EQ(done.terminals, 1);
        EXPECT_EQ(done.value, 2);
    }
    EXPECT_EQ(live_frames, 0);
}

TEST(ExecutionTaskIntegration, H102_DefaultTaskUnderSyncWait) {
    static_assert(std::same_as<ex::task<int>::start_scheduler_type, ex::task_scheduler>);
    live_frames = 0;
    {
        auto result = std::this_thread::sync_wait(default_value_task());
        ASSERT_TRUE(result);
        EXPECT_EQ(std::get<0>(*result), 61);
    }
    {
        int ran = 0;
        auto result = std::this_thread::sync_wait(default_void_task(&ran));
        EXPECT_TRUE(result);
        EXPECT_EQ(ran, 1);
    }
    {
        int mark = 0;
        try {
            (void)std::this_thread::sync_wait(default_throwing_task());
        } catch (const marked_error& marked) {
            mark = marked.mark;
        }
        EXPECT_EQ(mark, 62);
    }
    {
        // Stopped: sync_wait returns a disengaged optional.
        auto result = std::this_thread::sync_wait(default_stopped_task());
        EXPECT_FALSE(result);
    }
    EXPECT_EQ(live_frames, 0);
}

// A moved-from task holds no frame. It can only be destroyed.
TEST(ExecutionTaskIntegration, H102_MovedFromTaskIsDestroyedOnly) {
    live_frames = 0;
    events log;
    {
        auto original = ends_void<inline_environment>(&log);
        auto taken = std::move(original);
        // One frame, suspended at its start, owned by the second object.
        EXPECT_TRUE(log.empty());
        inline_variant variant;
        const outcome done = variant.run(std::move(taken));
        EXPECT_EQ(done.terminals, 1);
        EXPECT_EQ(log, (events{"body@0"}));
        // Both moved-from objects are destroyed here, and nothing else.
    }
    EXPECT_EQ(live_frames, 0);
    EXPECT_EQ(log.size(), 1u);
}

// ---------------------------------------------------------------------------
// H1-04: a finite trace of 64 awaits, and the typed-only terminate path

// The position of the machine stack at a point of the body, to see whether
// one await's resumption nests inside the previous one.
#if defined(_MSC_VER)
#define FORGE_TEST_NOINLINE __declspec(noinline)
#else
#define FORGE_TEST_NOINLINE __attribute__((noinline))
#endif

FORGE_TEST_NOINLINE std::uintptr_t stack_position() {
    volatile char here = 0;
    volatile char* volatile address = &here;
    return reinterpret_cast<std::uintptr_t>(address);
}

struct await_trace {
    events log;
    std::vector<std::uintptr_t> positions;
};

// How many of the recorded positions are further along the stack, in the
// given direction of growth, than every one before them. One means that
// nothing nested.
int observed_nesting(const std::vector<std::uintptr_t>& positions, bool grows_down) {
    int nesting = 1;
    std::uintptr_t deepest = positions.front();
    for (const std::uintptr_t position : positions) {
        if (grows_down ? position < deepest : position > deepest) {
            deepest = position;
            ++nesting;
        }
    }
    return nesting;
}

// The two controls for that measure: calls that do nest, and calls that
// follow one another.
FORGE_TEST_NOINLINE void nests(int depth, std::vector<std::uintptr_t>* positions) {
    [[maybe_unused]] volatile char frame[64] = {};
    positions->push_back(stack_position());
    if (depth > 1) nests(depth - 1, positions);
    frame[0] = static_cast<char>(positions->size());
}

FORGE_TEST_NOINLINE void follows(int count, std::vector<std::uintptr_t>* positions) {
    for (int i = 0; i != count; ++i) nests(1, positions);
}

// The measure is usable in this build when it tells the two controls apart:
// eight nested calls give 8 and eight calls in sequence give 1. It reports
// the direction of stack growth it found. Without that (for instance when
// locals are not kept on the machine stack) there is no measurement.
std::optional<bool> calibrated_stack_direction() {
    std::vector<std::uintptr_t> nested, flat;
    nests(8, &nested);
    follows(8, &flat);
    for (const bool grows_down : {true, false}) {
        if (observed_nesting(nested, grows_down) == 8 && observed_nesting(flat, grows_down) == 1 &&
            observed_nesting(nested, !grows_down) == 1) {
            return grows_down;
        }
    }
    return std::nullopt;
}

int_task<inline_environment> awaits_sixty_four_times(await_trace* trace, frame_marker) {
    // The body's own exit is a separate event from the frame's destruction.
    struct exit_marker {
        events* log;
        ~exit_marker() { log->push_back("body left"); }
    } at_exit{&trace->log};
    trace->log.push_back("body entry");
    for (int i = 0; i != 64; ++i) {
        trace->log.push_back("await " + std::to_string(i) + " begin");
        co_await ex::just();
        trace->log.push_back("await " + std::to_string(i) + " end");
        trace->positions.push_back(stack_position());
    }
    trace->log.push_back("normal return");
    co_return 64;
}

struct tracing_receiver {
    using receiver_concept = ex::receiver_tag;

    start_env<ex::inline_scheduler> get_env() const noexcept { return {}; }
    void set_value(int value) && noexcept {
        trace->log.push_back("value completion");
        out->value = value;
        out->entered("value");
    }
    void set_error(std::exception_ptr) && noexcept { out->entered("exception"); }
    void set_stopped() && noexcept { out->entered("stopped"); }

    await_trace* trace;
    outcome* out;
};

// Starts an operation when it is itself destroyed: a stand-in for an
// implementation that completes during teardown.
template<class Operation>
struct starts_at_destruction {
    explicit starts_at_destruction(Operation& operation) : operation(operation) {}
    starts_at_destruction(const starts_at_destruction&) = delete;
    ~starts_at_destruction() { ex::start(operation); }

    Operation& operation;
};

events expected_trace_of_sixty_four_awaits() {
    events expected{"body entry"};
    for (int i = 0; i != 64; ++i) {
        expected.push_back("await " + std::to_string(i) + " begin");
        expected.push_back("await " + std::to_string(i) + " end");
    }
    expected.push_back("normal return");
    expected.push_back("body left");
    // The frame, with its parameter copy, is destroyed before the receiver
    // is entered.
    expected.push_back("frame destroyed");
    expected.push_back("value completion");
    return expected;
}

TEST(ExecutionTaskIntegration, H104_SixtyFourAwaitsGiveOneFiniteOrderedTrace) {
    const events expected = expected_trace_of_sixty_four_awaits();
    ASSERT_EQ(expected.size(), 133u);
    await_trace trace;
    outcome done;
    {
        auto operation = ex::connect(awaits_sixty_four_times(&trace, frame_marker{&trace.log}),
                                     tracing_receiver{&trace, &done});
        EXPECT_TRUE(trace.log.empty());
        ex::start(operation); // the H1-04 start
        // The checkpoint: everything has happened when start returns, with
        // the operation still alive.
        EXPECT_EQ(trace.log, expected);
        EXPECT_EQ(done.terminals, 1);
        EXPECT_EQ(done.value, 64);
        EXPECT_EQ(trace.positions.size(), 64u);
    }
    // Destroying the operation adds nothing.
    EXPECT_EQ(trace.log, expected);
    EXPECT_EQ(done.terminals, 1);
    EXPECT_EQ(done.value, 64);

    {
        // The control for that checkpoint: the same task, started only while
        // its scope is left. The checkpoint sees nothing, and the totals
        // afterwards are the same as above.
        await_trace late;
        outcome late_done;
        {
            auto operation = ex::connect(awaits_sixty_four_times(&late, frame_marker{&late.log}),
                                         tracing_receiver{&late, &late_done});
            starts_at_destruction later{operation};
            EXPECT_TRUE(late.log.empty());
            EXPECT_EQ(late_done.terminals, 0);
        }
        EXPECT_EQ(late.log, expected);
        EXPECT_EQ(late_done.terminals, 1);
        EXPECT_EQ(late_done.value, 64);
    }

    // Nesting, observational: how many of the 64 resumptions ran further
    // along the machine stack than every one before them. One means that no
    // resumption nests inside an earlier one. A number is published only
    // when the measure passed its two controls in this build.
    ASSERT_EQ(trace.positions.size(), 64u);
    if (const auto grows_down = calibrated_stack_direction()) {
        const int nesting = observed_nesting(trace.positions, *grows_down);
        RecordProperty("maximum_observed_nesting", nesting);
        std::printf("H1-04 maximum observed nesting over 64 awaits: %d\n", nesting);
    } else {
        RecordProperty("maximum_observed_nesting", "unavailable");
        std::printf("H1-04 maximum observed nesting over 64 awaits: unavailable (measure not calibrated)\n");
    }
}

// A task whose error list has no exception_ptr. An awaited error that is an
// exception cannot be delivered, so the body's unhandled exception ends the
// program.
struct typed_only_environment {
    using start_scheduler_type = ex::inline_scheduler;
    using error_types = ex::completion_signatures<ex::set_error_t(int)>;
};

// The mark of the exception that the await threw into the body, for the
// terminate handler: 0 until the await has been evaluated and has thrown.
int mark_thrown_by_the_await = 0;

ex::task<int, typed_only_environment> typed_only_awaits_an_exception(bool returns_the_mark) {
    try {
        co_await ex::just_error(std::make_exception_ptr(marked_error{71}));
    } catch (const marked_error& marked) {
        mark_thrown_by_the_await = marked.mark;
        if (!returns_the_mark) throw;
    }
    co_return mark_thrown_by_the_await;
}

ex::task<int, typed_only_environment> typed_only_yields_its_error() {
    co_yield ex::with_error{72};
    co_return 0;
}

[[noreturn]] void run_typed_only_and_exit() {
    mark_thrown_by_the_await = 0;
    // 86 only for a termination after the await threw its exception. A
    // termination before that point exits with 87.
    std::set_terminate([] { std::_Exit(mark_thrown_by_the_await == 71 ? 86 : 87); });
    outcome done;
    auto operation =
        ex::connect(typed_only_awaits_an_exception(false), probe_receiver<ex::inline_scheduler>{&done, {}});
    ex::start(operation);
    // Reached only if a terminal was delivered instead.
    std::_Exit(94);
}

TEST(ExecutionTaskIntegrationDeathTest, H104_TypedOnlyTaskTerminatesOnAnAwaitedException) {
    {
        // The adjacent control: the same environment delivers its typed error.
        inline_variant variant;
        const outcome done = variant.run(typed_only_yields_its_error());
        EXPECT_EQ(done.terminals, 1);
        EXPECT_EQ(done.channel, "typed");
        EXPECT_EQ(done.typed_error, 72);
    }
    {
        // The positive for the await path: the same await throws the marked
        // exception into the body, which may catch it and go on.
        mark_thrown_by_the_await = 0;
        inline_variant variant;
        const outcome done = variant.run(typed_only_awaits_an_exception(true));
        EXPECT_EQ(done.terminals, 1);
        EXPECT_EQ(done.channel, "value");
        EXPECT_EQ(done.value, 71);
    }
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    EXPECT_EXIT(run_typed_only_and_exit(), ::testing::ExitedWithCode(86), "");
}

} // namespace
