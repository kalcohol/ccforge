// Compile controls for the configuration of std::execution::task
// ([task.class]/1 to /4). Without a FORGE_TASK_CONFIG_REJECT_* definition
// every fixture below is the accepted form. Each definition changes exactly
// one type, to the rejected one of its configuration class, and nothing
// else. Each fixture instantiates the requirement or the use. The
// requirements that connect checks on a complete task ([task.class]/4 in
// full) have their controls in the runtime tests and the mutation record.

#include <execution>

#include <cstddef>
#include <exception>
#include <memory>
#include <stop_token>
#include <utility>

namespace ex = std::execution;

struct receiver {
    using receiver_concept = ex::receiver_tag;

    struct env {
        ex::inline_scheduler query(ex::get_start_scheduler_t) const noexcept { return {}; }
    };

    env get_env() const noexcept { return {}; }
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

struct inline_environment {
    using start_scheduler_type = ex::inline_scheduler;
};

// 1. A cv-qualified object result.
#if defined(FORGE_TASK_CONFIG_REJECT_CV_RESULT)
using object_result = const int;
#else
using object_result = int;
#endif
static_assert(sizeof(ex::task<object_result, inline_environment>) != 0);

// 2. An array result.
#if defined(FORGE_TASK_CONFIG_REJECT_ARRAY_RESULT)
using element_result = int[2];
#else
using element_result = int;
#endif
static_assert(sizeof(ex::task<element_result, inline_environment>) != 0);

// 3. A nonclass environment.
#if defined(FORGE_TASK_CONFIG_REJECT_NONCLASS_ENVIRONMENT)
using environment_argument = int;
#else
using environment_argument = ex::env<>;
#endif
static_assert(sizeof(ex::task<int, environment_argument>) != 0);

// 4. An error list that is no completion_signatures specialization.
struct error_list_environment {
    using start_scheduler_type = ex::inline_scheduler;
#if defined(FORGE_TASK_CONFIG_REJECT_MALFORMED_ERROR_LIST)
    using error_types = int;
#else
    using error_types = ex::completion_signatures<ex::set_error_t(int)>;
#endif
};
ex::task<void, error_list_environment> uses_error_list() { co_return; }

// 5. A signature in the list that is no error signature.
struct error_signature_environment {
    using start_scheduler_type = ex::inline_scheduler;
#if defined(FORGE_TASK_CONFIG_REJECT_NON_ERROR_SIGNATURE)
    using error_types = ex::completion_signatures<ex::set_value_t(int)>;
#else
    using error_types = ex::completion_signatures<ex::set_error_t(int)>;
#endif
};
ex::task<void, error_signature_environment> uses_error_signature() { co_return; }

// 6. A start scheduler type that is no scheduler. The class is enough: the
// requirement that needs no complete type is checked there.
struct scheduler_environment {
#if defined(FORGE_TASK_CONFIG_REJECT_START_SCHEDULER)
    using start_scheduler_type = int;
#else
    using start_scheduler_type = ex::inline_scheduler;
#endif
};
static_assert(ex::scheduler<ex::inline_scheduler> && !ex::scheduler<int>);
static_assert(sizeof(ex::task<void, scheduler_environment>) != 0);

// 7. An allocator type that is no allocator. The class is enough here too,
// and no coroutine is defined, so frame allocation takes no part.
struct allocator_environment {
    using start_scheduler_type = ex::inline_scheduler;
#if defined(FORGE_TASK_CONFIG_REJECT_ALLOCATOR)
    using allocator_type = int;
#else
    using allocator_type = std::allocator<std::byte>;
#endif
};
static_assert(sizeof(ex::task<void, allocator_environment>) != 0);

// 8. A stop source type that is no stoppable source.
struct stop_source_environment {
    using start_scheduler_type = ex::inline_scheduler;
#if defined(FORGE_TASK_CONFIG_REJECT_STOP_SOURCE)
    using stop_source_type = int;
#else
    using stop_source_type = std::inplace_stop_source;
#endif
};
static_assert(sizeof(ex::task<void, stop_source_environment>) != 0);

int main() {
    run(uses_error_list());
    run(uses_error_signature());
}
