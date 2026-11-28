// Compile controls for the stop token of std::execution::task. Without a
// FORGE_TASK_STOP_REJECT_* definition the translation below is accepted.
// Each definition removes the one member that the accepted form relies on.

#include <execution>

#include <exception>
#include <type_traits>
#include <utility>

namespace ex = std::execution;

template<class Env>
struct receiver {
    using receiver_concept = ex::receiver_tag;

    Env get_env() const noexcept { return {}; }
    template<class... Values>
    void set_value(Values&&...) && noexcept {}
    void set_error(std::exception_ptr) && noexcept {}
    void set_stopped() && noexcept {}
};

// [task.state]/7 with the W2 disposition: translating a mismatched token that
// can stop needs one default constructed source of the task's source type.
struct lone_token {
    template<class Callback>
    struct callback_type {
        template<class Init>
#if defined(FORGE_TASK_STOP_REJECT_CALLBACK)
            requires std::is_empty_v<Callback>
#endif
        callback_type(const lone_token& token, Init&& init) : inner(token.inner, std::forward<Init>(init)) {}

        std::inplace_stop_callback<Callback> inner;
    };

    explicit lone_token(std::inplace_stop_token inner) noexcept : inner(inner) {}
    bool stop_requested() const noexcept { return inner.stop_requested(); }
    bool stop_possible() const noexcept { return inner.stop_possible(); }
    bool operator==(const lone_token&) const noexcept = default;

    std::inplace_stop_token inner;
};

struct other_token : lone_token {
    using lone_token::lone_token;
    bool operator==(const other_token&) const noexcept = default;
};

static_assert(std::stoppable_token<lone_token> && std::stoppable_token<other_token>);

struct lone_source {
    explicit lone_source(int) {}
#if !defined(FORGE_TASK_STOP_REJECT_SOURCE)
    lone_source() = default;
#endif

    other_token get_token() const noexcept { return other_token{inner.get_token()}; }
    bool stop_possible() const noexcept { return true; }
    bool stop_requested() const noexcept { return inner.stop_requested(); }
    bool request_stop() noexcept { return inner.request_stop(); }

    std::inplace_stop_source inner;
};

struct lone_environment {
    using start_scheduler_type = ex::inline_scheduler;
    using stop_source_type = lone_source;
};

// The receiver's token is a different type and can stop.
struct stop_env {
    lone_token query(std::get_stop_token_t) const noexcept { return lone_token{std::inplace_stop_token{}}; }
};

ex::task<void, lone_environment> uses_stop_translation() { co_return; }

// Q1. A source whose token factories give different types for an lvalue and
// for an rvalue source. The task's token type comes from the rvalue, and the
// translation issues from the stored lvalue. An exact match needs neither
// and is accepted. Every mismatch is rejected, also the tokenless one whose
// answer would be the in-place value.
// The lvalue's token converts to the rvalue's, so only the type check itself
// rejects the pairing.
struct sliced_token : std::inplace_stop_token {
    explicit sliced_token(std::inplace_stop_token inner) noexcept : std::inplace_stop_token(inner) {}
};

static_assert(std::stoppable_token<sliced_token>);
static_assert(std::convertible_to<sliced_token, std::inplace_stop_token>);

struct split_source {
    sliced_token get_token() const& noexcept { return sliced_token{inner.get_token()}; }
    std::inplace_stop_token get_token() && noexcept { return inner.get_token(); }
    bool stop_possible() const noexcept { return true; }
    bool stop_requested() const noexcept { return inner.stop_requested(); }
    bool request_stop() noexcept { return inner.request_stop(); }

    std::inplace_stop_source inner;
};

struct split_environment {
    using start_scheduler_type = ex::inline_scheduler;
    using stop_source_type = split_source;
};

struct exact_inplace_env {
    std::inplace_stop_token query(std::get_stop_token_t) const noexcept { return {}; }
};

struct tokenless_env {};

ex::task<void, split_environment> uses_split_source() { co_return; }

// X3. A token type that can never stop and has no default, issued by a
// source that only the application can construct. A reference-valued token
// of that very type is the answer and needs no candidate. A tokenless
// receiver has no candidate at all.
struct fixed_token {
    template<class Callback>
    struct callback_type {
        template<class Init>
        callback_type(const fixed_token&, Init&&) noexcept {}
    };

    explicit fixed_token(int) noexcept {}
    static constexpr bool stop_requested() noexcept { return false; }
    static constexpr bool stop_possible() noexcept { return false; }
    bool operator==(const fixed_token&) const noexcept = default;
};

struct application_source {
    struct application_t {};

    explicit application_source(application_t) noexcept {}
    fixed_token get_token() const noexcept { return fixed_token{0}; }
    static constexpr bool stop_possible() noexcept { return false; }
    static constexpr bool stop_requested() noexcept { return false; }
    bool request_stop() noexcept { return false; }
};

struct application_environment {
    using start_scheduler_type = ex::inline_scheduler;
    using stop_source_type = application_source;
};

inline const fixed_token shared_fixed_token{1};

struct fixed_reference_env {
    const fixed_token& query(std::get_stop_token_t) const noexcept { return shared_fixed_token; }
};

ex::task<void, application_environment> uses_application_source() { co_return; }

// Q5 with X3. The only candidate is the token's default value. It counts
// only if it cannot throw.
struct defaulted_token {
    template<class Callback>
    struct callback_type {
        template<class Init>
        callback_type(const defaulted_token& token, Init&& init) : inner(token.inner, std::forward<Init>(init)) {}

        std::inplace_stop_callback<Callback> inner;
    };

    defaulted_token()
#if !defined(FORGE_TASK_STOP_REJECT_THROWING_DEFAULT)
        noexcept
#endif
    {}
    explicit defaulted_token(std::inplace_stop_token inner) noexcept : inner(inner) {}
    bool stop_requested() const noexcept { return inner.stop_requested(); }
    bool stop_possible() const noexcept { return inner.stop_possible(); }
    bool operator==(const defaulted_token&) const noexcept = default;

    std::inplace_stop_token inner;
};

struct defaulted_token_source {
    struct application_t {};

    explicit defaulted_token_source(application_t) noexcept {}
    defaulted_token get_token() const noexcept { return defaulted_token{inner.get_token()}; }
    bool stop_possible() const noexcept { return true; }
    bool stop_requested() const noexcept { return inner.stop_requested(); }
    bool request_stop() noexcept { return inner.request_stop(); }

    std::inplace_stop_source inner;
};

struct defaulted_token_environment {
    using start_scheduler_type = ex::inline_scheduler;
    using stop_source_type = defaulted_token_source;
};

ex::task<void, defaulted_token_environment> uses_token_default() { co_return; }

// A source whose rvalue get_token() returns a reference. The task's token
// type would be a reference, which is rejected when the class is
// instantiated. The by-value sibling is accepted.
inline const std::inplace_stop_token shared_disengaged_token{};

struct reference_token_source {
    std::inplace_stop_token get_token() const& noexcept { return {}; }
#if defined(FORGE_TASK_STOP_REJECT_TOKEN_REFERENCE)
    const std::inplace_stop_token& get_token() && noexcept { return shared_disengaged_token; }
#else
    std::inplace_stop_token get_token() && noexcept { return {}; }
#endif
    static constexpr bool stop_possible() noexcept { return false; }
    static constexpr bool stop_requested() noexcept { return false; }
    bool request_stop() noexcept { return false; }
};

struct reference_token_environment {
    using start_scheduler_type = ex::inline_scheduler;
    using stop_source_type = reference_token_source;
};

static_assert(sizeof(ex::task<void, reference_token_environment>) != 0);

// The same requirement for a source whose rvalue get_token() returns void:
// the task's token type must be an object type. Only the class is
// instantiated, for the rejected form and for its by-value sibling.
struct void_token_source {
    std::inplace_stop_token get_token() const& noexcept { return {}; }
#if defined(FORGE_TASK_STOP_REJECT_TOKEN_VOID)
    void get_token() && noexcept {}
#else
    std::inplace_stop_token get_token() && noexcept { return {}; }
#endif
    static constexpr bool stop_possible() noexcept { return false; }
    static constexpr bool stop_requested() noexcept { return false; }
    bool request_stop() noexcept { return false; }
};

struct void_token_environment {
    using start_scheduler_type = ex::inline_scheduler;
    using stop_source_type = void_token_source;
};

static_assert(sizeof(ex::task<void, void_token_environment>) != 0);

#if !defined(FORGE_TASK_STOP_REJECT_TOKEN_REFERENCE)
ex::task<void, reference_token_environment> uses_reference_token_source() { co_return; }
#endif

template<class Task, class Env>
void run(Task task) {
    auto operation = ex::connect(std::move(task), receiver<Env>{});
    ex::start(operation);
}

int main() {
    run<ex::task<void, lone_environment>, stop_env>(uses_stop_translation());
#if defined(FORGE_TASK_STOP_REJECT_FACTORIES)
    run<ex::task<void, split_environment>, tokenless_env>(uses_split_source());
#else
    run<ex::task<void, split_environment>, exact_inplace_env>(uses_split_source());
#endif
#if defined(FORGE_TASK_STOP_REJECT_NO_CANDIDATE)
    run<ex::task<void, application_environment>, tokenless_env>(uses_application_source());
#else
    run<ex::task<void, application_environment>, fixed_reference_env>(uses_application_source());
#endif
    run<ex::task<void, defaulted_token_environment>, tokenless_env>(uses_token_default());
#if !defined(FORGE_TASK_STOP_REJECT_TOKEN_REFERENCE)
    // The rejected form is pinned where only the class is instantiated.
    run<ex::task<void, reference_token_environment>, tokenless_env>(uses_reference_token_source());
#endif
}
