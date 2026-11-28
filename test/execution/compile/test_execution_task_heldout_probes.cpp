// Compile controls for the held-out cases of std::execution::task (pilot
// J-01). Without a FORGE_HELDOUT_* definition every form below is the
// accepted sibling. Each rejecting definition changes one of them into the
// form that must not compile. The *_CONTROLS definitions keep the changed
// type and leave out only the rejected use, to show that the type alone is
// well-formed.

#include <execution>

#include <cstddef>
#include <exception>
#include <type_traits>
#include <utility>

namespace ex = std::execution;

template<class... Errors>
struct error_config {
    using start_scheduler_type = ex::inline_scheduler;
    using error_types = ex::completion_signatures<ex::set_error_t(Errors)...>;
};

struct receiver {
    using receiver_concept = ex::receiver_tag;

    template<class... Values>
    void set_value(Values&&...) && noexcept {}
    template<class Error>
    void set_error(Error&&) && noexcept {}
    void set_stopped() && noexcept {}
};

template<class Task>
void run(Task task) {
    auto operation = ex::connect(std::move(task), receiver{});
    ex::start(operation);
}

struct typed_error {
    int cookie;
};

struct other_error {
    int cookie;
};

// ---------------------------------------------------------------------------
// A04: an immovable payload whose rvalue converts to the one listed error.

struct fixed_payload {
    fixed_payload() = default;
#if defined(FORGE_HELDOUT_A04_IMMOVABLE) || defined(FORGE_HELDOUT_A04_IMMOVABLE_CONTROLS)
    fixed_payload(const fixed_payload&) = delete;
    fixed_payload(fixed_payload&&) = delete;
#else
    fixed_payload(fixed_payload&&) = default;
#endif

    operator typed_error() && { return typed_error{4}; }
};

// Both controls hold for the immovable form as well.
static_assert(std::is_aggregate_v<ex::with_error<fixed_payload>>);
static_assert(std::is_convertible_v<fixed_payload, typed_error>);
static_assert(!std::is_convertible_v<fixed_payload, other_error>);
#if defined(FORGE_HELDOUT_A04_IMMOVABLE) || defined(FORGE_HELDOUT_A04_IMMOVABLE_CONTROLS)
static_assert(!std::is_move_constructible_v<fixed_payload>);
#endif

int forms_the_aggregate_prvalue() {
    // The prvalue initializes its member directly, without a move.
    typed_error converted = ex::with_error<fixed_payload>{fixed_payload{}}.error.operator typed_error();
    typed_error from_rvalue = fixed_payload{};
    return converted.cookie + from_rvalue.cookie;
}

#if !defined(FORGE_HELDOUT_A04_IMMOVABLE_CONTROLS)
ex::task<void, error_config<typed_error, other_error>> yields_fixed_payload() {
    co_yield ex::with_error<fixed_payload>{fixed_payload{}};
}
#endif

// ---------------------------------------------------------------------------
// A05: exactly one implicitly reachable listed error type.

struct convertible_payload {
#if defined(FORGE_HELDOUT_A05_EXPLICIT_ONLY)
    explicit operator typed_error() && { return typed_error{5}; }
#else
    operator typed_error() && { return typed_error{5}; }
#endif
#if defined(FORGE_HELDOUT_A05_TWO_TARGETS)
    operator other_error() && { return other_error{6}; }
#endif
};

ex::task<void, error_config<typed_error, other_error>> yields_convertible_payload() {
    co_yield ex::with_error<convertible_payload>{convertible_payload{}};
}

// ---------------------------------------------------------------------------
// A23: a start scheduler with none of the five initializers.

template<class Scheduler>
struct schedule_sender {
    using sender_concept = ex::sender_tag;

    template<class Receiver>
    struct operation {
        using operation_state_concept = ex::operation_state_tag;

        Receiver receiver;

        explicit operation(Receiver receiver) : receiver(std::move(receiver)) {}
        operation(operation&&) = delete;

        void start() & noexcept { ex::set_value(std::move(receiver)); }
    };

    struct attributes {
        Scheduler scheduler;

        template<class... Env>
        Scheduler query(ex::get_completion_scheduler_t<ex::set_value_t>, Env&&...) const noexcept {
            return scheduler;
        }
    };

    Scheduler scheduler;

    template<class Self, class... Env>
    static consteval auto get_completion_signatures() -> ex::completion_signatures<ex::set_value_t()> {
        return {};
    }

    template<ex::receiver Receiver>
    operation<Receiver> connect(Receiver receiver) const {
        return operation<Receiver>(std::move(receiver));
    }

    attributes get_env() const noexcept { return {scheduler}; }
};

struct incoming_scheduler {
    using scheduler_concept = ex::scheduler_tag;

    schedule_sender<incoming_scheduler> schedule() const noexcept { return {*this}; }
    ex::forward_progress_guarantee query(ex::get_forward_progress_guarantee_t) const noexcept {
        return ex::forward_progress_guarantee::weakly_parallel;
    }
    friend bool operator==(const incoming_scheduler&, const incoming_scheduler&) noexcept = default;
};

struct seed_t {};

// Still a scheduler: an unrelated seed constructor, copy, equality and
// schedule. It accepts neither the incoming scheduler nor an allocator.
struct seeded_scheduler {
    using scheduler_concept = ex::scheduler_tag;

    explicit seeded_scheduler(seed_t) noexcept {}
#if !defined(FORGE_HELDOUT_A23_NO_INITIALIZER) && !defined(FORGE_HELDOUT_A23_NO_INITIALIZER_CONTROLS)
    seeded_scheduler() noexcept = default;
#endif

    schedule_sender<seeded_scheduler> schedule() const noexcept { return {*this}; }
    ex::forward_progress_guarantee query(ex::get_forward_progress_guarantee_t) const noexcept {
        return ex::forward_progress_guarantee::weakly_parallel;
    }
    friend bool operator==(const seeded_scheduler&, const seeded_scheduler&) noexcept = default;
};

static_assert(ex::scheduler<seeded_scheduler>);
static_assert(ex::scheduler<incoming_scheduler>);
static_assert(!std::is_constructible_v<seeded_scheduler, incoming_scheduler>);
static_assert(!std::is_constructible_v<seeded_scheduler, std::allocator<std::byte>>);
static_assert(!std::is_constructible_v<seeded_scheduler, incoming_scheduler, std::allocator<std::byte>>);
#if defined(FORGE_HELDOUT_A23_NO_INITIALIZER) || defined(FORGE_HELDOUT_A23_NO_INITIALIZER_CONTROLS)
static_assert(!std::is_default_constructible_v<seeded_scheduler>);
#endif

struct seeded_config {
    using start_scheduler_type = seeded_scheduler;
};

// A different scheduler and a valid scheduler allocator both exist.
struct offering_env {
    incoming_scheduler query(ex::get_start_scheduler_t) const noexcept { return {}; }
    std::allocator<std::byte> query(std::get_scheduler_allocator_t) const noexcept { return {}; }
    std::allocator<std::byte> query(std::get_allocator_t) const noexcept { return {}; }
};

struct offering_receiver {
    using receiver_concept = ex::receiver_tag;

    template<class... Values>
    void set_value(Values&&...) && noexcept {}
    template<class Error>
    void set_error(Error&&) && noexcept {}
    void set_stopped() && noexcept {}
    offering_env get_env() const noexcept { return {}; }
};

ex::task<void, seeded_config> runs_on_seeded_scheduler() {
    co_return;
}

// The class, the coroutine and connect are well-formed either way. start is
// what selects the scheduler.
void connects_and_starts_on_seeded_scheduler() {
    auto operation = ex::connect(runs_on_seeded_scheduler(), offering_receiver{});
#if !defined(FORGE_HELDOUT_A23_NO_INITIALIZER_CONTROLS)
    ex::start(operation);
#endif
}

// ---------------------------------------------------------------------------
// A24: a const lvalue reference result does not bind a temporary. The task is
// the literal task<const int&>, with the default environment. The directly
// connected receiver supplies an inline start scheduler.

int external_int = 42;

struct inline_start_receiver {
    using receiver_concept = ex::receiver_tag;

    template<class... Values>
    void set_value(Values&&...) && noexcept {}
    template<class Error>
    void set_error(Error&&) && noexcept {}
    void set_stopped() && noexcept {}
    auto get_env() const noexcept { return ex::prop{ex::get_start_scheduler, ex::inline_scheduler{}}; }
};

ex::task<const int&> returns_const_int_reference(int* external) {
#if defined(FORGE_HELDOUT_A24_TEMPORARY)
    (void)external;
    co_return 42;
#else
    co_return *external;
#endif
}

int main() {
    (void)forms_the_aggregate_prvalue();
#if !defined(FORGE_HELDOUT_A04_IMMOVABLE_CONTROLS)
    run(yields_fixed_payload());
#endif
    run(yields_convertible_payload());
    connects_and_starts_on_seeded_scheduler();
    auto operation = ex::connect(returns_const_int_reference(&external_int), inline_start_receiver{});
    ex::start(operation);
}
