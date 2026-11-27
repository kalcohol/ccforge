// Compile controls for the environments of std::execution::task. Without a
// FORGE_TASK_ENV_REJECT_* definition every case below is the accepted
// sibling. Each definition removes the one constructor or member that the
// accepted form relies on.

#include <execution>

#include <cstddef>
#include <exception>
#include <memory>
#include <utility>

namespace ex = std::execution;

struct receiver_env {};

template<class Env>
struct receiver {
    using receiver_concept = ex::receiver_tag;

    Env get_env() const noexcept { return {}; }
    template<class... Values>
    void set_value(Values&&...) && noexcept {}
    void set_error(std::exception_ptr) && noexcept {}
    void set_stopped() && noexcept {}
};

template<class Task, class Env = receiver_env>
void run(Task task) {
    auto operation = ex::connect(std::move(task), receiver<Env>{});
    ex::start(operation);
}

// Connects and destroys the operation without starting it.
template<class Task, class Env = receiver_env>
void connect_only(Task task) {
    [[maybe_unused]] auto operation = ex::connect(std::move(task), receiver<Env>{});
}

// [task.state]/2, third item: own-env from get_env(rcvr), else own-env().
struct own_env_environment {
    using start_scheduler_type = ex::inline_scheduler;

    template<class ReceiverEnv>
    struct env_type {
        explicit env_type(int) {}
#if !defined(FORGE_TASK_ENV_REJECT_OWN_ENV)
        env_type() = default;
#endif
    };
};

ex::task<void, own_env_environment> uses_own_env() { co_return; }

// [task.state]/2, fourth item: Environment from own-env, else from
// get_env(rcvr), else Environment().
struct constructed_environment {
    using start_scheduler_type = ex::inline_scheduler;

    explicit constructed_environment(int) {}
#if !defined(FORGE_TASK_ENV_REJECT_ENVIRONMENT)
    constructed_environment() = default;
#endif
};

ex::task<void, constructed_environment> uses_environment() { co_return; }

// [task.members]/5: allocator_type from the receiver's allocator, else
// allocator_type().
template<class T>
struct picky_allocator {
    using value_type = T;

    explicit picky_allocator(std::allocator<int>) noexcept {}
    template<class U>
    picky_allocator(const picky_allocator<U>&) noexcept {}

    T* allocate(std::size_t count) { return std::allocator<T>{}.allocate(count); }
    void deallocate(T* pointer, std::size_t count) noexcept { std::allocator<T>{}.deallocate(pointer, count); }
    template<class U>
    bool operator==(const picky_allocator<U>&) const noexcept { return true; }
};

struct picky_environment {
    using start_scheduler_type = ex::inline_scheduler;
    using allocator_type = picky_allocator<std::byte>;
};

struct allocator_env {
#if !defined(FORGE_TASK_ENV_REJECT_ALLOCATOR)
    std::allocator<int> query(std::get_allocator_t) const noexcept { return {}; }
#endif
};

// The frame itself is allocated with an explicit allocator, so only the
// connect-time Mandate depends on the receiver. The operation is connected
// and destroyed without being started: the Mandate belongs to connect.
ex::task<void, picky_environment> uses_allocator(std::allocator_arg_t, std::allocator<int>) { co_return; }

// [task.state]/4: none of the five initializers is valid for a scheduler
// type that is otherwise a scheduler.
struct stubborn_scheduler {
    using scheduler_concept = ex::scheduler_tag;

    explicit stubborn_scheduler(int) noexcept {}
#if !defined(FORGE_TASK_ENV_REJECT_START_SCHEDULER)
    stubborn_scheduler() noexcept = default;
#endif

    auto schedule() const noexcept { return ex::schedule(ex::inline_scheduler{}); }
    auto query(ex::get_forward_progress_guarantee_t) const noexcept {
        return ex::forward_progress_guarantee::weakly_parallel;
    }
    bool operator==(const stubborn_scheduler&) const noexcept = default;
};

static_assert(ex::scheduler<stubborn_scheduler>);

struct stubborn_environment {
    using start_scheduler_type = stubborn_scheduler;
};

struct scheduler_env {
    ex::inline_scheduler query(ex::get_start_scheduler_t) const noexcept { return {}; }
    std::allocator<int> query(std::get_scheduler_allocator_t) const noexcept { return {}; }
};

ex::task<void, stubborn_environment> uses_start_scheduler() { co_return; }

// A reference start_scheduler_type is rejected by this backport, which is
// stricter than [task.class]/4. The accepted sibling is the object type.
struct aliased_scheduler_environment {
#if defined(FORGE_TASK_ENV_REJECT_REFERENCE_SCHEDULER)
    using start_scheduler_type = ex::inline_scheduler&;
#else
    using start_scheduler_type = ex::inline_scheduler;
#endif
};

inline ex::inline_scheduler shared_inline_scheduler;

struct lvalue_scheduler_env {
    ex::inline_scheduler& query(ex::get_start_scheduler_t) const noexcept { return shared_inline_scheduler; }
};

ex::task<void, aliased_scheduler_environment> uses_scheduler_alias() { co_return; }

// The same rejection where only the class is instantiated: no coroutine, no
// connect and no start.
struct class_only_environment {
#if defined(FORGE_TASK_ENV_REJECT_REFERENCE_SCHEDULER_CLASS)
    using start_scheduler_type = const ex::inline_scheduler&;
#else
    using start_scheduler_type = ex::inline_scheduler;
#endif
};

static_assert(sizeof(ex::task<void, class_only_environment>) != 0);

// [exec.get.scheduler.allocator]/2: the selected member must not throw. A
// potentially throwing scheduler-specific member is an error. It does not
// fall back to the ordinary allocator member, which is valid here.
struct selected_member_env {
    std::allocator<int> query(std::get_scheduler_allocator_t) const
#if !defined(FORGE_TASK_ENV_REJECT_THROWING_SCHEDULER_ALLOCATOR)
        noexcept
#endif
    {
        return {};
    }
    std::allocator<long> query(std::get_allocator_t) const noexcept { return {}; }
};

int main() {
    run(uses_own_env());
    run(uses_environment());
    connect_only<ex::task<void, picky_environment>, allocator_env>(
        uses_allocator(std::allocator_arg, std::allocator<int>{}));
    run<ex::task<void, stubborn_environment>, scheduler_env>(uses_start_scheduler());
    run<ex::task<void, aliased_scheduler_environment>, lvalue_scheduler_env>(uses_scheduler_alias());
    [[maybe_unused]] auto allocator = std::get_scheduler_allocator(selected_member_env{});
}
