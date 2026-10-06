#include "stopped_optional_probe.hpp"

#include <concepts>
#include <exception>
#include <memory>
#include <tuple>
#include <type_traits>

using namespace stopped_optional_probe;

template<class S>
concept direct_optional = requires(S&& source) {
    ex::stopped_as_optional(static_cast<S&&>(source));
};

template<class S>
concept piped_optional = requires(S&& source) {
    static_cast<S&&>(source) | ex::stopped_as_optional;
};

template<class S>
concept closure_optional = requires(S&& source) {
    static_cast<S&&>(source) | ex::stopped_as_optional();
};

template<class S>
constexpr bool accepts_all_forms =
    direct_optional<S> && piped_optional<S> && closure_optional<S>;

template<class S, class Env>
concept direct_optional_in = requires(S&& source, Env&& env) {
    ex::get_completion_signatures(
        ex::stopped_as_optional(static_cast<S&&>(source)), static_cast<Env&&>(env));
};

template<class S, class Env>
concept piped_optional_in = requires(S&& source, Env&& env) {
    ex::get_completion_signatures(
        static_cast<S&&>(source) | ex::stopped_as_optional, static_cast<Env&&>(env));
};

template<class S, class Env>
concept closure_optional_in = requires(S&& source, Env&& env) {
    ex::get_completion_signatures(
        static_cast<S&&>(source) | ex::stopped_as_optional(), static_cast<Env&&>(env));
};

template<class S, class Env>
constexpr bool accepts_all_forms_in =
    direct_optional_in<S, Env> && piped_optional_in<S, Env> && closure_optional_in<S, Env>;

template<class S, class Env>
constexpr bool rejects_all_forms_in =
    !direct_optional_in<S, Env> && !piped_optional_in<S, Env> && !closure_optional_in<S, Env>;

using single_sender = signature_sender<ex::completion_signatures<
    ex::set_value_t(int), ex::set_error_t(long), ex::set_stopped_t()>>;
using tuple_sender = signature_sender<ex::completion_signatures<
    ex::set_value_t(int, double), ex::set_stopped_t()>>;
using empty_tuple_sender = signature_sender<ex::completion_signatures<
    ex::set_value_t(std::tuple<>), ex::set_stopped_t()>>;
using zero_sender = signature_sender<ex::completion_signatures<>>;
using void_sender = signature_sender<ex::completion_signatures<ex::set_value_t()>>;
using error_sender = signature_sender<ex::completion_signatures<ex::set_error_t(int)>>;
using stopped_sender = signature_sender<ex::completion_signatures<ex::set_stopped_t()>>;
using multi_sender = signature_sender<ex::completion_signatures<
    ex::set_value_t(int), ex::set_value_t(double)>>;
using same_decay_sender = signature_sender<ex::completion_signatures<
    ex::set_value_t(int), ex::set_value_t(const int&)>>;
using mixed_void_sender = signature_sender<ex::completion_signatures<
    ex::set_value_t(), ex::set_value_t(int)>>;

static_assert(accepts_all_forms<single_sender>);
static_assert(accepts_all_forms<tuple_sender>);
static_assert(accepts_all_forms<empty_tuple_sender>);
static_assert(accepts_all_forms<zero_sender>);
static_assert(accepts_all_forms<void_sender>);
static_assert(accepts_all_forms<error_sender>);
static_assert(accepts_all_forms<stopped_sender>);
static_assert(accepts_all_forms<multi_sender>);
static_assert(accepts_all_forms<same_decay_sender>);
static_assert(accepts_all_forms<mixed_void_sender>);
static_assert(accepts_all_forms_in<single_sender, ex::empty_env>);
static_assert(accepts_all_forms_in<tuple_sender, ex::empty_env>);
static_assert(accepts_all_forms_in<empty_tuple_sender, ex::empty_env>);
static_assert(rejects_all_forms_in<zero_sender, ex::empty_env>);
static_assert(rejects_all_forms_in<void_sender, ex::empty_env>);
static_assert(rejects_all_forms_in<error_sender, ex::empty_env>);
static_assert(rejects_all_forms_in<stopped_sender, ex::empty_env>);
static_assert(rejects_all_forms_in<multi_sender, ex::empty_env>);
static_assert(rejects_all_forms_in<same_decay_sender, ex::empty_env>);
static_assert(rejects_all_forms_in<mixed_void_sender, ex::empty_env>);

using single_optional = decltype(ex::stopped_as_optional(single_sender{}));
using tuple_optional = decltype(ex::stopped_as_optional(tuple_sender{}));
using empty_tuple_optional = decltype(ex::stopped_as_optional(empty_tuple_sender{}));
static_assert(std::same_as<ex::completion_signatures_of_t<single_optional>,
    ex::completion_signatures<ex::set_value_t(std::optional<int>), ex::set_error_t(long)>>);
static_assert(std::same_as<ex::completion_signatures_of_t<tuple_optional>,
    ex::completion_signatures<ex::set_value_t(std::optional<std::tuple<int, double>>)>>);
static_assert(std::same_as<ex::completion_signatures_of_t<empty_tuple_optional>,
    ex::completion_signatures<ex::set_value_t(std::optional<std::tuple<>>)>>);
static_assert(!ex::sends_stopped<single_optional>);

struct throwing_move {
    throwing_move() = default;
    throwing_move(throwing_move&&) noexcept(false) {}
    throwing_move(const throwing_move&) = delete;
};
using throwing_sender = signature_sender<ex::completion_signatures<
    ex::set_value_t(throwing_move), ex::set_stopped_t()>>;
using throwing_optional = decltype(ex::stopped_as_optional(throwing_sender{}));
static_assert(std::same_as<ex::completion_signatures_of_t<throwing_optional>,
    ex::completion_signatures<ex::set_value_t(std::optional<throwing_move>),
        ex::set_error_t(std::exception_ptr)>>);
using move_only_optional = decltype(ex::stopped_as_optional(ex::just(std::unique_ptr<int>{})));
static_assert(std::same_as<ex::completion_signatures_of_t<move_only_optional>,
    ex::completion_signatures<ex::set_value_t(std::optional<std::unique_ptr<int>>)>>);

static_assert(!ex::sender_in<dependent_sender>);
static_assert(accepts_all_forms<dependent_sender>);
static_assert(accepts_all_forms_in<dependent_sender, value_env>);
static_assert(rejects_all_forms_in<dependent_sender, void_env>);
static_assert(rejects_all_forms_in<dependent_sender, no_value_env>);
static_assert(rejects_all_forms_in<dependent_sender, multi_env>);
using dependent_optional = decltype(ex::stopped_as_optional(dependent_sender{}));
static_assert(!ex::sender_in<dependent_optional>);
static_assert(ex::sender_in<dependent_optional, value_env>);
static_assert(!ex::sender_in<dependent_optional, void_env>);
static_assert(!ex::sender_in<dependent_optional, no_value_env>);
static_assert(!ex::sender_in<dependent_optional, multi_env>);
static_assert(std::same_as<ex::completion_signatures_of_t<dependent_optional, value_env>,
    ex::completion_signatures<ex::set_value_t(std::optional<int>)>>);

template<class S, class Env>
concept member_connectable = requires(S&& source) {
    static_cast<S&&>(source).connect(receiver<Env>{});
};

static_assert(member_connectable<dependent_optional, value_env>);
static_assert(member_connectable<const dependent_optional&, value_env>);
static_assert(ex::sender_to<dependent_optional, receiver<value_env>>);
static_assert(!member_connectable<dependent_optional, void_env>);
static_assert(!member_connectable<const dependent_optional&, void_env>);
static_assert(!member_connectable<dependent_optional, no_value_env>);
static_assert(!member_connectable<const dependent_optional&, no_value_env>);
static_assert(!member_connectable<dependent_optional, multi_env>);
static_assert(!member_connectable<const dependent_optional&, multi_env>);

struct scheduler_env {
    auto query(ex::get_scheduler_t) const noexcept -> ex::inline_scheduler { return {}; }
};

using scheduler_source = decltype(ex::read_env(ex::get_scheduler));
static_assert(accepts_all_forms<scheduler_source>);
static_assert(accepts_all_forms_in<scheduler_source, scheduler_env>);
using scheduler_optional = decltype(ex::stopped_as_optional(ex::read_env(ex::get_scheduler)));
static_assert(std::same_as<ex::completion_signatures_of_t<scheduler_optional, scheduler_env>,
    ex::completion_signatures<ex::set_value_t(std::optional<ex::inline_scheduler>)>>);
static_assert(member_connectable<scheduler_optional, scheduler_env>);
static_assert(member_connectable<const scheduler_optional&, scheduler_env>);

struct empty_void_query {
    void operator()(ex::empty_env) const noexcept {}
    int operator()(value_env) const noexcept { return 42; }
};

static_assert(std::is_void_v<std::invoke_result_t<empty_void_query&, ex::empty_env>>);
static_assert(std::same_as<std::invoke_result_t<empty_void_query&, value_env>, int>);
using empty_void_source = decltype(ex::read_env(empty_void_query{}));
static_assert(accepts_all_forms<empty_void_source>);
static_assert(accepts_all_forms_in<empty_void_source, value_env>);
using empty_void_optional = decltype(ex::stopped_as_optional(ex::read_env(empty_void_query{})));
static_assert(std::same_as<ex::completion_signatures_of_t<empty_void_optional, value_env>,
    ex::completion_signatures<ex::set_value_t(std::optional<int>)>>);
static_assert(member_connectable<empty_void_optional, value_env>);
static_assert(member_connectable<const empty_void_optional&, value_env>);

int main() {
    auto direct = ex::stopped_as_optional(ex::just(42));
    auto piped = ex::just(1, 2) | ex::stopped_as_optional;
    auto closure = ex::just(std::tuple<>{}) | ex::stopped_as_optional();
    (void)direct;
    (void)piped;
    (void)closure;
}
