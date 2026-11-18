#include <execution>

#include "../scheduler_fixture.hpp"

#include <concepts>
#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L
#include <coroutine>
#endif
#include <exception>
#include <memory>
#include <type_traits>
#include <utility>

namespace ex = std::execution;

namespace {
using completions = ex::completion_signatures<ex::set_value_t(int),
                                              ex::set_error_t(std::exception_ptr),
                                              ex::set_stopped_t()>;

struct legacy_receiver {
    using receiver_concept = ex::receiver_tag;

    friend void tag_invoke(ex::set_value_t, legacy_receiver&&, int) noexcept {}
    friend void tag_invoke(ex::set_error_t, legacy_receiver&&, std::exception_ptr) noexcept {}
    friend void tag_invoke(ex::set_stopped_t, legacy_receiver&&) noexcept {}
};

struct member_receiver {
    using receiver_concept = ex::receiver_tag;

    void set_value(int) && noexcept {}
    void set_error(std::exception_ptr) && noexcept {}
    void set_stopped() && noexcept {}
};

static_assert(ex::receiver<legacy_receiver>);
static_assert(!ex::__forge_receiver_of<legacy_receiver, completions>);
static_assert(ex::__forge_receiver_of<member_receiver, completions>);
static_assert(!std::invocable<ex::set_value_t, legacy_receiver, int>);
static_assert(!std::invocable<ex::set_error_t, legacy_receiver, std::exception_ptr>);
static_assert(!std::invocable<ex::set_stopped_t, legacy_receiver>);
static_assert(std::invocable<ex::set_value_t, member_receiver, int>);
static_assert(std::invocable<ex::set_error_t, member_receiver, std::exception_ptr>);
static_assert(std::invocable<ex::set_stopped_t, member_receiver>);

struct member_operation {
    using operation_state_concept = ex::operation_state_tag;
    void start() & noexcept {}
};

struct legacy_operation {
    using operation_state_concept = ex::operation_state_tag;
    friend void tag_invoke(ex::start_t, legacy_operation&) noexcept {}
};

static_assert(!ex::operation_state<legacy_operation>);
static_assert(!std::invocable<ex::start_t, legacy_operation&>);
static_assert(ex::operation_state<member_operation>);
static_assert(std::invocable<ex::start_t, member_operation&>);

struct unmarked_legacy_sender {
    friend member_operation tag_invoke(ex::connect_t, unmarked_legacy_sender, member_receiver) noexcept {
        return {};
    }

    friend completions tag_invoke(ex::get_completion_signatures_t, unmarked_legacy_sender, ex::env<>) noexcept {
        return {};
    }
};

struct marked_legacy_sender : unmarked_legacy_sender {
    using sender_concept = ex::sender_tag;
};

struct member_sender {
    using sender_concept = ex::sender_tag;

    template<class Self, class... Env>
    static constexpr auto get_completion_signatures() noexcept -> completions { return {}; }

    member_operation connect(member_receiver) const noexcept { return {}; }
};

static_assert(!ex::sender<unmarked_legacy_sender>);
static_assert(ex::sender<marked_legacy_sender>);
static_assert(!ex::sender_in<marked_legacy_sender, ex::env<>>);
static_assert(!std::invocable<ex::connect_t, marked_legacy_sender, member_receiver>);
static_assert(ex::sender_in<member_sender, ex::env<>>);
static_assert(std::invocable<ex::connect_t, member_sender, member_receiver>);
static_assert(std::same_as<ex::completion_signatures_of_t<member_sender, ex::env<>>, completions>);

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L
struct awaitable_with_adl {
    bool await_ready() const noexcept { return true; }
    void await_suspend(std::coroutine_handle<>) const noexcept {}
    int await_resume() const noexcept { return 42; }

    friend ex::completion_signatures<ex::set_value_t(short)>
    tag_invoke(ex::get_completion_signatures_t, awaitable_with_adl, ex::env<>) noexcept {
        return {};
    }

    struct legacy_operation {
        using operation_state_concept = ex::operation_state_tag;
        void start() & noexcept {}
    };

    friend legacy_operation tag_invoke(ex::connect_t, awaitable_with_adl, member_receiver) noexcept {
        return {};
    }
};

// [exec.getcomplsigs]/3 and [exec.connect]/2 select the awaitable fallback.
static_assert(std::same_as<ex::completion_signatures_of_t<awaitable_with_adl, ex::env<>>,
                           completions>);
static_assert(ex::operation_state<ex::connect_result_t<awaitable_with_adl, member_receiver>>);
static_assert(!noexcept(ex::connect(awaitable_with_adl{}, member_receiver{})));
#endif

struct legacy_scheduler {
    using scheduler_concept = ex::scheduler_tag;

    auto query(ex::get_forward_progress_guarantee_t) const noexcept -> ex::forward_progress_guarantee {
        return ex::forward_progress_guarantee::weakly_parallel;
    }
    friend auto tag_invoke(ex::schedule_t, const legacy_scheduler&) noexcept {
        return ex::inline_scheduler{}.schedule();
    }
    bool operator==(const legacy_scheduler&) const = default;
};

static_assert(!ex::scheduler<legacy_scheduler>);
static_assert(!std::invocable<ex::schedule_t, const legacy_scheduler&>);
static_assert(ex::scheduler<ex::inline_scheduler>);
static_assert(std::invocable<ex::schedule_t, const ex::inline_scheduler&>);

struct custom_domain {};
using completion_scheduler = ex::get_completion_scheduler_t<ex::set_value_t>;
using completion_domain = ex::get_completion_domain_t<ex::set_value_t>;

template<class Query, class Result>
struct legacy_query_env {
    friend Result tag_invoke(Query, const legacy_query_env&) noexcept { return {}; }
};

template<class Query, class Result>
struct member_query_env {
    Result query(const Query&) const noexcept { return {}; }
};

template<class Query, class Result>
constexpr bool member_only =
    !std::invocable<Query, const legacy_query_env<Query, Result>&> &&
    std::invocable<Query, const member_query_env<Query, Result>&>;

static_assert(member_only<ex::get_start_scheduler_t, ex::inline_scheduler>);
static_assert(member_only<ex::get_scheduler_t, ex::inline_scheduler>);
static_assert(member_only<ex::get_delegation_scheduler_t, ex::inline_scheduler>);
static_assert(member_only<std::get_allocator_t, std::allocator<int>>);
static_assert(member_only<ex::get_await_completion_adaptor_t, int>);
static_assert(member_only<completion_scheduler, ex::inline_scheduler>);
static_assert(member_only<completion_domain, custom_domain>);

using legacy_domain_env = legacy_query_env<ex::get_domain_t, custom_domain>;
using member_domain_env = member_query_env<ex::get_domain_t, custom_domain>;
// [exec.get.domain]/2.3 selects the fallback when there is no member.
static_assert(std::same_as<decltype(ex::get_domain(legacy_domain_env{})), ex::default_domain>);
static_assert(std::same_as<decltype(ex::get_domain(member_domain_env{})), custom_domain>);

struct scheduler_domain {};
struct domain_scheduler {
    using scheduler_concept = ex::scheduler_tag;

    constexpr auto schedule() const noexcept -> scheduler_fixture::schedule_sender<domain_scheduler> {
        return {*this};
    }
    constexpr auto query(ex::get_forward_progress_guarantee_t) const noexcept {
        return ex::forward_progress_guarantee::weakly_parallel;
    }
    constexpr domain_scheduler query(completion_scheduler) const noexcept { return *this; }
    constexpr scheduler_domain query(completion_domain) const noexcept { return {}; }
    constexpr bool operator==(const domain_scheduler&) const noexcept = default;
};
struct legacy_domain_with_scheduler : legacy_domain_env {
    constexpr domain_scheduler query(ex::get_scheduler_t) const noexcept { return {}; }
};
// [exec.get.domain]/2.2 reaches the scheduler through HIDE-SCHED.
static_assert(std::same_as<decltype(ex::get_domain(legacy_domain_with_scheduler{})),
                           scheduler_domain>);
static_assert(scheduler_fixture::agrees_with_schedule(domain_scheduler{}));
static_assert(scheduler_fixture::agrees_with_schedule(domain_scheduler{}, ex::env<>{}));
static_assert(scheduler_fixture::agrees_with_schedule(domain_scheduler{},
    ex::__forge_domain::__hide_scheduler<legacy_domain_with_scheduler>{nullptr}));

using legacy_token_env = legacy_query_env<std::get_stop_token_t, std::inplace_stop_token>;
using member_token_env = member_query_env<std::get_stop_token_t, std::inplace_stop_token>;
static_assert(std::same_as<decltype(std::get_stop_token(legacy_token_env{})), std::never_stop_token>);
static_assert(std::same_as<decltype(std::get_stop_token(member_token_env{})), std::inplace_stop_token>);

struct overload_env {
    std::never_stop_token query(const std::get_stop_token_t&) const& noexcept { return {}; }
    std::inplace_stop_token query(std::get_stop_token_t&) const& noexcept { return {}; }
    std::inplace_stop_token query(std::get_stop_token_t&&) const& noexcept { return {}; }
    std::inplace_stop_token query(const std::get_stop_token_t&) & noexcept { return {}; }
};

struct runtime_forwarding {
    bool value;

    constexpr bool operator()(const ex::env<>&) const noexcept { return value; }
    constexpr bool query(std::forwarding_query_t) & noexcept { return false; }
    constexpr bool query(std::forwarding_query_t) && noexcept { return false; }
    constexpr bool query(std::forwarding_query_t) const& noexcept { return value; }
};

constexpr runtime_forwarding forwarding_object{true};
// [exec.fwd.env]/2.1 evaluates the supplied object, including its cv/ref.
static_assert(std::forwarding_query(forwarding_object));
static_assert(!std::forwarding_query(runtime_forwarding{true}));

struct original_object_query {
    const original_object_query* original = this;

    constexpr bool operator()(const ex::env<>&) const noexcept { return true; }
    constexpr bool query(std::forwarding_query_t) const noexcept { return this == original; }
};
constexpr original_object_query original_query;
static_assert(std::is_trivially_copy_constructible_v<original_object_query>);
static_assert(std::forwarding_query(original_query));

struct derived_scheduler_query : ex::get_scheduler_t {
    friend constexpr bool tag_invoke(std::forwarding_query_t, derived_scheduler_query) noexcept {
        return false;
    }
};
// The base dispatch selected the exact ADL hook. The WD selects the inherited member.
static_assert(std::forwarding_query(derived_scheduler_query{}));

} // namespace

int main() {
    overload_env environment;
    // [exec.get.stop.token]/2.1 uses both a const environment and a const lvalue tag.
    static_assert(std::same_as<decltype(std::get_stop_token(environment)), std::never_stop_token>);
}
