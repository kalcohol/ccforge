// Compile controls for std::execution::task. Without a FORGE_TASK_REJECT_*
// definition every coroutine below is the accepted sibling. Each definition
// changes one coroutine into the form the task must reject.

#include <execution>

#include <exception>
#include <utility>

namespace ex = std::execution;

struct inline_environment {
    using start_scheduler_type = ex::inline_scheduler;
};

template<class... Errors>
struct error_environment {
    using start_scheduler_type = ex::inline_scheduler;
    using error_types = ex::completion_signatures<ex::set_error_t(Errors)...>;
};

int external_int = 17;

// F-03: an lvalue reference result follows optional<U&>::emplace.
ex::task<const int&, inline_environment> returns_const_reference() {
#if defined(FORGE_TASK_REJECT_TEMPORARY_REFERENCE)
    co_return 42;
#else
    co_return external_int;
#endif
}

struct narrow_error {
    int code;
};

struct first_error {
    first_error(narrow_error) {}
};

struct second_error {
    second_error(narrow_error) {}
};

struct unrelated_error {};

// F-05: the payload must reach exactly one listed error type.
#if defined(FORGE_TASK_REJECT_NO_ERROR_TARGET)
using target_environment = error_environment<unrelated_error>;
#elif defined(FORGE_TASK_REJECT_AMBIGUOUS_ERROR_TARGET)
using target_environment = error_environment<first_error, second_error>;
#else
using target_environment = error_environment<first_error, unrelated_error>;
#endif

ex::task<void, target_environment> yields_narrow_error() {
    co_yield ex::with_error{narrow_error{3}};
}

// F-05: staging moves the payload, so an immovable one is rejected even
// though its rvalue converts to the single listed error type.
struct pinned_error {
    explicit pinned_error(int code) : code(code) {}
#if defined(FORGE_TASK_REJECT_IMMOVABLE_ERROR)
    pinned_error(pinned_error&&) = delete;
#else
    pinned_error(pinned_error&&) = default;
#endif
    int code;
};

struct from_pinned {
    from_pinned(pinned_error&& source) : code(source.code) {}
    int code;
};

ex::task<void, error_environment<from_pinned>> yields_pinned_error() {
    co_yield ex::with_error<pinned_error>{pinned_error{4}};
}

// [task.class]/1 admits void, not cv-qualified void.
#if defined(FORGE_TASK_REJECT_CV_VOID)
using void_task = ex::task<const void, inline_environment>;
#else
using void_task = ex::task<void, inline_environment>;
#endif
static_assert(sizeof(void_task) > 0);

// H1-04, [task.promise]/6: an await operand is a sender. An operand that is
// neither a sender nor an awaitable is rejected.
struct not_awaitable {};

ex::task<void, inline_environment> awaits_an_operand() {
#if defined(FORGE_TASK_REJECT_NON_SENDER_AWAIT)
    co_await not_awaitable{};
#else
    co_await ex::just();
#endif
}

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

int main() {
    run(returns_const_reference());
    run(yields_narrow_error());
    run(yields_pinned_error());
    run(awaits_an_operand());
}
