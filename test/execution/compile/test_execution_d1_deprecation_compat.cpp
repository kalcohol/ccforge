#ifndef FORGE_DEPRECATION_CASE
#define FORGE_DEPRECATION_CASE 0
#endif

// The dispatch branch warnings are located at the dependent calls in these headers.
#if FORGE_DEPRECATION_CASE == 0 || (defined(FORGE_SUPPRESS_DEPRECATION) && (FORGE_DEPRECATION_CASE == 17 || FORGE_DEPRECATION_CASE == 25))
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4996)
#elif defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
#endif
#include <forge/bulk.hpp>
#include <forge/execution_extensions.hpp>
#include <forge/detail/execution_concepts.hpp>
#if FORGE_DEPRECATION_CASE == 0 || (defined(FORGE_SUPPRESS_DEPRECATION) && (FORGE_DEPRECATION_CASE == 17 || FORGE_DEPRECATION_CASE == 25))
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
#endif

#include "../composition_probe.hpp"
#include "../sender_adaptor_closure_probe.hpp"
#include "stopped_optional_probe.hpp"

#include <concepts>
#include <functional>
#include <memory>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace ex = std::execution;

template<class S, class T, class Value>
bool same_value(S old, T replacement, const Value& expected) {
    auto old_value = forge::sync_wait(std::move(old));
    auto new_value = forge::sync_wait(std::move(replacement));
    return old_value && new_value &&
        std::get<0>(*old_value) == expected && old_value == new_value;
}

template<class S>
bool observes_stop(S sender, bool expected) {
    std::inplace_stop_source source;
    source.request_stop();
    auto result = forge::sync_wait(ex::write_env(std::move(sender),
        ex::prop(std::get_stop_token, source.get_token())));
    return result && std::get<0>(*result) == std::pair{expected, expected};
}

struct stop_flags {
    auto operator()(const auto& token) const noexcept -> std::pair<bool, bool> {
        return {token.stop_possible(), token.stop_requested()};
    }
};

struct read_stop_token {
    auto operator()() const { return ex::read_env(std::get_stop_token); }
};

struct make_value {
    int operator()() const noexcept { return 42; }
};

struct legacy_callable {
    template<ex::sender S>
    auto operator()(S&& source) const -> decltype(ex::then(std::forward<S>(source), make_value{})) {
        return ex::then(std::forward<S>(source), make_value{});
    }
};

struct legacy_pipe : legacy_callable {
    template<ex::sender S>
    friend auto operator|(S&& source, legacy_pipe closure)
        -> decltype(legacy_callable{}(std::forward<S>(source))) {
        return closure(std::forward<S>(source));
    }
};

struct each_index {
    void operator()(int index, int& value) const noexcept { value += index; }
};

struct each_chunk {
    void operator()(int begin, int end, int& value) const noexcept {
        for (int i = begin; i != end; ++i) value += i;
    }
};

template<class Domain>
struct domain_env {
    auto query(ex::get_domain_t) const noexcept -> Domain { return {}; }
};

template<class Domain>
struct domain_attrs {
    auto query(ex::get_completion_domain_t<ex::set_value_t>) const noexcept -> Domain { return {}; }
};

template<class Domain>
struct domain_sender {
    using sender_concept = ex::sender_tag;
    int* calls;

    auto get_env() const noexcept -> domain_attrs<Domain> { return {}; }
};

struct legacy_domain {
    template<class S, class E>
        requires requires(S&& source) { source.calls; }
    S&& transform_sender(S&& source, const E&) const noexcept {
        ++*source.calls;
        return std::forward<S>(source);
    }
};

struct tagged_domain {
    template<class Tag, class S, class E>
        requires requires(S&& source) { source.calls; }
    S&& transform_sender(Tag, S&& source, const E&) const noexcept {
        ++*source.calls;
        return std::forward<S>(source);
    }
};

struct domain_receiver {
    using receiver_concept = ex::receiver_tag;
    void set_value(int) && noexcept {}
    auto get_env() const noexcept -> domain_env<tagged_domain> { return {}; }
};

static_assert(ex::receiver<domain_receiver>);

struct stops_with_value_signature {
    using sender_concept = ex::sender_tag;

    template<class Self, class Env>
    static constexpr auto get_completion_signatures() noexcept
        -> ex::completion_signatures<ex::set_value_t(int), ex::set_stopped_t()> { return {}; }

    template<ex::receiver R>
    auto connect(R rcvr) && -> ex::connect_result_t<decltype(ex::just_stopped()), R> {
        return ex::connect(ex::just_stopped(), std::move(rcvr));
    }
};

template<bool HasQuery>
struct progress_scheduler {
    using scheduler_concept = ex::scheduler_tag;
    constexpr bool operator==(const progress_scheduler&) const noexcept = default;

    template<class... Envs>
    constexpr auto query(ex::get_completion_scheduler_t<ex::set_value_t>, const Envs&...) const noexcept
        -> progress_scheduler { return *this; }

    struct schedule_sender {
        using sender_concept = ex::sender_tag;

        template<class Self, class... Envs>
        static constexpr auto get_completion_signatures() noexcept
            -> ex::completion_signatures<ex::set_value_t()> { return {}; }

        constexpr auto get_env() const noexcept -> progress_scheduler { return {}; }

        template<ex::receiver R>
        auto connect(R rcvr) const noexcept(noexcept(ex::connect(ex::just(), std::move(rcvr))))
            -> ex::connect_result_t<decltype(ex::just()), R> {
            return ex::connect(ex::just(), std::move(rcvr));
        }
    };

    constexpr auto schedule() const noexcept -> schedule_sender { return {}; }
    constexpr auto query(ex::get_forward_progress_guarantee_t) const noexcept
        -> ex::forward_progress_guarantee requires HasQuery {
        return ex::forward_progress_guarantee::weakly_parallel;
    }
};

#if FORGE_DEPRECATION_CASE == 0
// These compatibility spellings are deprecated in documentation only.
using recv = domain_receiver;
using sndr = decltype(ex::just(42));
static_assert(std::same_as<ex::receiver_t, ex::receiver_tag>);
static_assert(std::same_as<ex::sender_t, ex::sender_tag>);
static_assert(std::same_as<ex::scheduler_t, ex::scheduler_tag>);
static_assert(std::same_as<ex::operation_state_t, ex::operation_state_tag>);
static_assert(std::same_as<decltype(ex::make_env(std::declval<domain_env<tagged_domain>>())),
                          decltype(ex::env{std::declval<domain_env<tagged_domain>>()})>);
static_assert(ex::queryable<domain_env<tagged_domain>> == std::destructible<domain_env<tagged_domain>>);
static_assert(ex::receiver_of<recv, ex::completion_signatures<>> ==
              forge::__detail::__receiver_of<recv, ex::completion_signatures<>>);
static_assert(ex::sender_to<sndr, recv>);
static_assert(forge::__detail::__sender_to<sndr, recv>);
#endif

#if FORGE_DEPRECATION_CASE == 0 || defined(FORGE_SUPPRESS_DEPRECATION)
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4996)
#elif defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
#endif

namespace {
using composition_probe::copyable_matrix;

template<class C>
constexpr bool public_base = std::derived_from<std::remove_cvref_t<C>,
    ex::sender_adaptor_closure<std::remove_cvref_t<C>>>;

#if FORGE_DEPRECATION_CASE == 0 || FORGE_DEPRECATION_CASE == 1
static_assert(copyable_matrix<decltype(ex::write_env(forge::empty_env{}))>());
static_assert(public_base<decltype(ex::write_env(ex::env<>{}))>);
#endif
#if FORGE_DEPRECATION_CASE == 0 || FORGE_DEPRECATION_CASE == 2
static_assert(copyable_matrix<decltype(ex::unstoppable())>());
static_assert(public_base<decltype(ex::unstoppable())>);
#endif
#if FORGE_DEPRECATION_CASE == 0 || FORGE_DEPRECATION_CASE == 3
static_assert(public_base<decltype(ex::unstoppable)>);
static_assert(public_closure_probe::pipeable<decltype(ex::just(1)), decltype(ex::unstoppable)>);
#endif
#if FORGE_DEPRECATION_CASE == 0
static_assert(copyable_matrix<decltype(ex::unstoppable)>());
#endif
#if FORGE_DEPRECATION_CASE == 0 || FORGE_DEPRECATION_CASE == 6
static_assert(copyable_matrix<decltype(ex::into_variant())>());
static_assert(public_base<decltype(ex::into_variant())>);

using legacy_variant_closure = decltype(ex::into_variant());
template<class C>
constexpr bool excludes_variant_wrapper =
    !public_closure_probe::pipeable<public_closure_probe::literal_sender, C> &&
    !public_closure_probe::pipeable<public_closure_probe::identity, C> &&
    !public_closure_probe::pipeable<C, public_closure_probe::identity>;

static_assert(excludes_variant_wrapper<std::reference_wrapper<legacy_variant_closure>>);
static_assert(excludes_variant_wrapper<std::reference_wrapper<const legacy_variant_closure>>);
static_assert(requires(legacy_variant_closure& closure, public_closure_probe::literal_sender value) {
    std::ref(closure)(std::move(value));
    std::cref(closure)(std::move(value));
});
#endif
#if FORGE_DEPRECATION_CASE == 0 || FORGE_DEPRECATION_CASE == 7
static_assert(copyable_matrix<decltype(ex::stopped_as_optional())>());
static_assert(public_base<decltype(ex::stopped_as_optional())>);

namespace legacy_optional {
namespace ex = std::execution;
using namespace stopped_optional_probe;

template<class S>
concept closure_optional = requires(S&& source) {
    static_cast<S&&>(source) | ex::stopped_as_optional();
};

template<class S, class Env>
concept closure_optional_in = requires(S&& source, Env&& env) {
    ex::get_completion_signatures(
        static_cast<S&&>(source) | ex::stopped_as_optional(), static_cast<Env&&>(env));
};

static_assert(closure_optional<single_sender>);
static_assert(closure_optional<tuple_sender>);
static_assert(closure_optional<empty_tuple_sender>);
static_assert(closure_optional<zero_sender>);
static_assert(closure_optional<void_sender>);
static_assert(closure_optional<error_sender>);
static_assert(closure_optional<stopped_sender>);
static_assert(closure_optional<multi_sender>);
static_assert(closure_optional<same_decay_sender>);
static_assert(closure_optional<mixed_void_sender>);
static_assert(closure_optional_in<single_sender, forge::empty_env>);
static_assert(closure_optional_in<tuple_sender, forge::empty_env>);
static_assert(closure_optional_in<empty_tuple_sender, forge::empty_env>);
static_assert(!closure_optional_in<zero_sender, forge::empty_env>);
static_assert(!closure_optional_in<void_sender, forge::empty_env>);
static_assert(!closure_optional_in<error_sender, forge::empty_env>);
static_assert(!closure_optional_in<stopped_sender, forge::empty_env>);
static_assert(!closure_optional_in<multi_sender, forge::empty_env>);
static_assert(!closure_optional_in<same_decay_sender, forge::empty_env>);
static_assert(!closure_optional_in<mixed_void_sender, forge::empty_env>);
static_assert(closure_optional<dependent_sender>);
static_assert(closure_optional_in<dependent_sender, value_env>);
static_assert(!closure_optional_in<dependent_sender, void_env>);
static_assert(!closure_optional_in<dependent_sender, no_value_env>);
static_assert(!closure_optional_in<dependent_sender, multi_env>);

using scheduler_source = decltype(ex::read_env(ex::get_scheduler));
using empty_void_source = decltype(ex::read_env(empty_void_query{}));
static_assert(closure_optional<scheduler_source>);
static_assert(closure_optional_in<scheduler_source, scheduler_env>);
static_assert(closure_optional<empty_void_source>);
static_assert(closure_optional_in<empty_void_source, value_env>);

template<class S>
bool reads_sync_wait_scheduler(S sender) {
    auto result = forge::sync_wait(std::move(sender) | ex::then(
        [](std::optional<ex::run_loop::scheduler> scheduler) noexcept {
            return scheduler.has_value();
        }));
    return result && std::get<0>(*result);
}

template<class S>
bool reads_value_from_receiver_env(S sender) {
    static_assert(std::same_as<ex::completion_signatures_of_t<S, runtime::optional_env>,
        ex::completion_signatures<ex::set_value_t(std::optional<int>)>>);
    runtime::observed_result copied;
    {
        auto op = ex::connect(std::as_const(sender), runtime::optional_receiver{&copied});
        ex::start(op);
    }
    runtime::observed_result moved;
    auto move_op = ex::connect(std::move(sender), runtime::optional_receiver{&moved});
    ex::start(move_op);
    auto is_expected = [](const runtime::observed_result& result) {
        return result.values == 1 && result.errors == 0 && result.engaged && result.value == 42;
    };
    return is_expected(copied) && is_expected(moved);
}
} // namespace legacy_optional
#endif

#if FORGE_DEPRECATION_CASE == 0 || FORGE_DEPRECATION_CASE == 8
namespace legacy_on {
using source = decltype(ex::schedule(ex::inline_scheduler{}));

struct identity_adaptor {
    template<ex::sender S>
    auto operator()(S&& sender) && -> std::decay_t<S> {
        return std::forward<S>(sender);
    }
};

struct copyable_adaptor : identity_adaptor {
    constexpr copyable_adaptor() = default;
    constexpr copyable_adaptor(copyable_adaptor&) noexcept {}
    constexpr copyable_adaptor(const copyable_adaptor&) noexcept {}
    constexpr copyable_adaptor(copyable_adaptor&&) noexcept {}
    constexpr copyable_adaptor(const copyable_adaptor&&) noexcept {}
};

struct move_only_adaptor : identity_adaptor {
    move_only_adaptor() = default;
    move_only_adaptor(move_only_adaptor&&) noexcept = default;
    move_only_adaptor(const move_only_adaptor&) = delete;
};

struct throwing_move_adaptor : identity_adaptor {
    throwing_move_adaptor() = default;
    throwing_move_adaptor(const throwing_move_adaptor&) noexcept = default;
    throwing_move_adaptor(throwing_move_adaptor&&) noexcept(false) {}
};

template<class C>
concept bindable = requires(C&& closure) {
    ex::on(ex::inline_scheduler{}, std::forward<C>(closure));
};

static_assert(bindable<move_only_adaptor>);
static_assert(!bindable<move_only_adaptor&>);
static_assert(!bindable<const move_only_adaptor&>);
static_assert(!bindable<const move_only_adaptor>);

template<class C>
using bound_t = decltype(ex::on(ex::inline_scheduler{}, C{}));

using move_only_bound = bound_t<move_only_adaptor>;
static_assert(!std::invocable<move_only_bound&, source>);
static_assert(!std::invocable<const move_only_bound&, source>);
static_assert(!std::invocable<const move_only_bound&&, source>);
static_assert(!public_closure_probe::pipeable<source, move_only_bound&>);
static_assert(!public_closure_probe::pipeable<source, const move_only_bound&>);
static_assert(!public_closure_probe::pipeable<source, const move_only_bound&&>);
static_assert(composition_probe::applicable<move_only_bound&&, source>);

using copyable_bound = bound_t<copyable_adaptor>;
using throwing_bound = bound_t<throwing_move_adaptor>;
static_assert(std::is_nothrow_invocable_v<copyable_bound&, source>);
static_assert(std::is_nothrow_invocable_v<copyable_bound&&, source>);
static_assert(std::is_nothrow_invocable_v<throwing_bound&, source>);
static_assert(!std::is_nothrow_invocable_v<throwing_bound&&, source>);
static_assert(noexcept(std::declval<source>() | std::declval<copyable_bound&&>()));
static_assert(!noexcept(std::declval<source>() | std::declval<throwing_bound&&>()));
static_assert(!noexcept(ex::on(ex::inline_scheduler{}, std::declval<throwing_move_adaptor>())));

static_assert([] {
    auto bound = ex::on(ex::inline_scheduler{}, copyable_adaptor{});
    auto first = bound(public_closure_probe::literal_sender{0});
    auto second = public_closure_probe::literal_sender{0} | std::move(std::as_const(bound));
    (void)first;
    (void)second;
    return true;
}());

// Both deprecated overloads are needed here. Keep the warning probes isolated.
#if FORGE_DEPRECATION_CASE == 0
using applied_t = decltype(std::declval<copyable_bound&>()(std::declval<source>()));
using full_t = decltype(ex::on(std::declval<source>(),
    std::declval<ex::inline_scheduler&>(), std::declval<copyable_adaptor&>()));
static_assert(std::same_as<applied_t, full_t>);
#endif
} // namespace legacy_on
#endif
} // namespace

int main() {
#if FORGE_DEPRECATION_CASE == 0 || FORGE_DEPRECATION_CASE == 1
    {
        const auto injected = ex::env{ex::prop(ex::get_scheduler, ex::inline_scheduler{})};
        auto old = ex::read_env(ex::get_scheduler) | ex::write_env(injected);
        auto replacement = ex::write_env(ex::read_env(ex::get_scheduler), injected);
        if (!same_value(std::move(old), std::move(replacement), ex::inline_scheduler{})) return 1;
    }
#endif
#if FORGE_DEPRECATION_CASE == 0 || FORGE_DEPRECATION_CASE == 2
    {
        auto old = ex::read_env(std::get_stop_token) | ex::unstoppable();
        auto replacement = ex::unstoppable(ex::read_env(std::get_stop_token));
        if (!observes_stop(ex::read_env(std::get_stop_token) | ex::then(stop_flags{}), true) ||
            !observes_stop(std::move(old) | ex::then(stop_flags{}), false) ||
            !observes_stop(std::move(replacement) | ex::then(stop_flags{}), false)) return 2;
    }
#endif
#if FORGE_DEPRECATION_CASE == 0 || FORGE_DEPRECATION_CASE == 3
    {
        auto old = ex::read_env(std::get_stop_token) | ex::unstoppable;
        auto replacement = ex::unstoppable(ex::read_env(std::get_stop_token));
        if (!observes_stop(ex::read_env(std::get_stop_token) | ex::then(stop_flags{}), true) ||
            !observes_stop(std::move(old) | ex::then(stop_flags{}), false) ||
            !observes_stop(std::move(replacement) | ex::then(stop_flags{}), false) ||
            !same_value(ex::just(42) | ex::unstoppable, ex::unstoppable(ex::just(42)), 42)) return 3;
    }
#endif
#if FORGE_DEPRECATION_CASE == 0 || FORGE_DEPRECATION_CASE == 4
    {
        auto old = ex::read_env(std::get_stop_token) | (ex::unstoppable | ex::then(stop_flags{}));
        auto replacement = ex::unstoppable(ex::read_env(std::get_stop_token)) | ex::then(stop_flags{});
        if (!observes_stop(ex::read_env(std::get_stop_token) | ex::then(stop_flags{}), true) ||
            !observes_stop(std::move(old), false) ||
            !observes_stop(std::move(replacement), false)) return 4;
    }
#endif
#if FORGE_DEPRECATION_CASE == 0 || FORGE_DEPRECATION_CASE == 5
    {
        auto old = ex::just() | (ex::let_value(read_stop_token{}) | ex::unstoppable);
        auto replacement = ex::unstoppable(ex::just() | ex::let_value(read_stop_token{}));
        auto unwrapped = ex::just() | ex::let_value(read_stop_token{});
        if (!observes_stop(std::move(unwrapped) | ex::then(stop_flags{}), true) ||
            !observes_stop(std::move(old) | ex::then(stop_flags{}), false) ||
            !observes_stop(std::move(replacement) | ex::then(stop_flags{}), false)) return 5;
    }
#endif
#if FORGE_DEPRECATION_CASE == 0 || FORGE_DEPRECATION_CASE == 6
    {
        auto old = ex::just(42) | ex::into_variant();
        auto replacement = ex::just(42) | ex::into_variant;
        using result_type = std::variant<std::tuple<int>>;
        if (!same_value(std::move(old), std::move(replacement), result_type{std::tuple{42}})) return 6;
    }
#endif
#if FORGE_DEPRECATION_CASE == 0 || FORGE_DEPRECATION_CASE == 7
    {
        auto old = ex::just(42) | ex::stopped_as_optional();
        auto replacement = ex::just(42) | ex::stopped_as_optional;
        if (!same_value(std::move(old), std::move(replacement), std::optional<int>{42})) return 7;
        auto stopped = stops_with_value_signature{} | ex::stopped_as_optional();
        auto stopped_replacement = stops_with_value_signature{} | ex::stopped_as_optional;
        if (!same_value(std::move(stopped), std::move(stopped_replacement), std::optional<int>{})) return 7;

        if (!same_value(ex::just(std::tuple<>{}) | ex::stopped_as_optional(),
                ex::just(std::tuple<>{}) | ex::stopped_as_optional,
                std::optional<std::tuple<>>{std::in_place})) return 7;
        if (!legacy_optional::reads_sync_wait_scheduler(ex::read_env(ex::get_scheduler) | ex::stopped_as_optional()) ||
            !legacy_optional::reads_sync_wait_scheduler(ex::read_env(ex::get_scheduler) | ex::stopped_as_optional)) return 7;

        using namespace stopped_optional_probe::runtime;
        static_assert(std::is_void_v<std::invoke_result_t<empty_void_query&, forge::empty_env>>);
        static_assert(std::same_as<std::invoke_result_t<empty_void_query&, optional_env>, int>);
        if (!legacy_optional::reads_value_from_receiver_env(ex::read_env(empty_void_query{}) | ex::stopped_as_optional()) ||
            !legacy_optional::reads_value_from_receiver_env(ex::read_env(empty_void_query{}) | ex::stopped_as_optional)) return 7;
    }
#endif
#if FORGE_DEPRECATION_CASE == 0 || FORGE_DEPRECATION_CASE == 8
    {
        const auto scheduler = ex::inline_scheduler{};
        (void)ex::on(scheduler, 42);
        auto old = ex::schedule(scheduler) | ex::on(scheduler, legacy_callable{});
        auto piped = ex::schedule(scheduler) | ex::on(scheduler, legacy_pipe{});
        if (!same_value(std::move(old), ex::schedule(scheduler) | ex::on(scheduler, ex::then(make_value{})), 42) ||
            !same_value(std::move(piped), ex::schedule(scheduler) | ex::on(scheduler, ex::then(make_value{})), 42)) return 8;
    }
#endif
#if FORGE_DEPRECATION_CASE == 0 || FORGE_DEPRECATION_CASE == 9
    {
        const auto scheduler = ex::inline_scheduler{};
        (void)ex::on(ex::schedule(scheduler), scheduler, 42);
        auto old = ex::on(ex::schedule(scheduler), scheduler, legacy_callable{});
        auto piped = ex::on(ex::schedule(scheduler), scheduler, legacy_pipe{});
        if (!same_value(std::move(old), ex::on(ex::schedule(scheduler), scheduler, ex::then(make_value{})), 42) ||
            !same_value(std::move(piped), ex::on(ex::schedule(scheduler), scheduler, ex::then(make_value{})), 42)) return 9;
    }
#endif
#if FORGE_DEPRECATION_CASE == 0 || FORGE_DEPRECATION_CASE == 10
    {
        auto old = ex::bulk(ex::just(39), 3, each_index{});
        auto replacement = forge::bulk(ex::just(39), 3, each_index{});
        static_assert(std::same_as<decltype(old), decltype(replacement)>);
        // Evaluate the factory without starting the sender. The original
        // specialization keeps the explicit const shape in its type.
        using source = decltype(ex::just(39));
        auto const_shape = ex::bulk.operator()<source, const int, each_index>(ex::just(39), 3, each_index{});
        static_assert(std::same_as<decltype(const_shape.__shape), const int>);
        if (!same_value(std::move(old), std::move(replacement), 42)) return 10;
    }
#endif
#if FORGE_DEPRECATION_CASE == 0 || FORGE_DEPRECATION_CASE == 11
    {
        auto old = ex::just(39) | ex::bulk(3, each_index{});
        auto replacement = ex::just(39) | forge::bulk(3, each_index{});
        static_assert(std::same_as<decltype(old), decltype(replacement)>);
        auto const_shape = ex::bulk.operator()<const int, each_index>(3, each_index{});
        static_assert(std::same_as<decltype(const_shape.__shape_), const int>);
        if (!same_value(std::move(old), std::move(replacement), 42)) return 11;
    }
#endif
#if FORGE_DEPRECATION_CASE == 0 || FORGE_DEPRECATION_CASE == 12
    {
        auto old = ex::bulk_chunked(ex::just(39), 3, each_chunk{});
        auto replacement = forge::bulk_chunked(ex::just(39), 3, each_chunk{});
        static_assert(std::same_as<decltype(old), decltype(replacement)>);
        using source = decltype(ex::just(39));
        auto const_shape = ex::bulk_chunked.operator()<source, const int, each_chunk>(ex::just(39), 3, each_chunk{});
        static_assert(std::same_as<decltype(const_shape.__shape), const int>);
        if (!same_value(std::move(old), std::move(replacement), 42)) return 12;
    }
#endif
#if FORGE_DEPRECATION_CASE == 0 || FORGE_DEPRECATION_CASE == 13
    {
        auto old = ex::just(39) | ex::bulk_chunked(3, each_chunk{});
        auto replacement = ex::just(39) | forge::bulk_chunked(3, each_chunk{});
        static_assert(std::same_as<decltype(old), decltype(replacement)>);
        auto const_shape = ex::bulk_chunked.operator()<const int, each_chunk>(3, each_chunk{});
        static_assert(std::same_as<decltype(const_shape.__shape_), const int>);
        if (!same_value(std::move(old), std::move(replacement), 42)) return 13;
    }
#endif
#if FORGE_DEPRECATION_CASE == 0 || FORGE_DEPRECATION_CASE == 14
    {
        auto old = ex::bulk_unchunked(ex::just(39), 3, each_index{});
        auto replacement = forge::bulk_unchunked(ex::just(39), 3, each_index{});
        static_assert(std::same_as<decltype(old), decltype(replacement)>);
        using source = decltype(ex::just(39));
        auto const_shape = ex::bulk_unchunked.operator()<source, const int, each_index>(ex::just(39), 3, each_index{});
        static_assert(std::same_as<decltype(const_shape.__shape), const int>);
        if (!same_value(std::move(old), std::move(replacement), 42)) return 14;
    }
#endif
#if FORGE_DEPRECATION_CASE == 0 || FORGE_DEPRECATION_CASE == 15
    {
        auto old = ex::just(39) | ex::bulk_unchunked(3, each_index{});
        auto replacement = ex::just(39) | forge::bulk_unchunked(3, each_index{});
        static_assert(std::same_as<decltype(old), decltype(replacement)>);
        auto const_shape = ex::bulk_unchunked.operator()<const int, each_index>(3, each_index{});
        static_assert(std::same_as<decltype(const_shape.__shape_), const int>);
        if (!same_value(std::move(old), std::move(replacement), 42)) return 15;
    }
#endif
#if FORGE_DEPRECATION_CASE == 0 || FORGE_DEPRECATION_CASE == 16
    {
        auto source = ex::just(42);
        auto& old = ex::default_domain{}.transform_sender(source, ex::env<>{});
        auto& replacement = ex::default_domain{}.transform_sender(ex::set_value, source, ex::env<>{});
        if (std::addressof(old) != std::addressof(source) ||
            std::addressof(old) != std::addressof(replacement)) return 16;
    }
#endif
#if FORGE_DEPRECATION_CASE == 0 || FORGE_DEPRECATION_CASE == 17
    {
        int old_calls = 0;
        int replacement_calls = 0;
        auto old = ex::transform_sender(domain_sender<legacy_domain>{&old_calls}, domain_env<legacy_domain>{});
        auto replacement = ex::transform_sender(domain_sender<tagged_domain>{&replacement_calls}, domain_env<tagged_domain>{});
        if (old_calls != 2 || replacement_calls != 2 || old.calls != std::addressof(old_calls) ||
            replacement.calls != std::addressof(replacement_calls)) return 17;
    }
#endif
#if FORGE_DEPRECATION_CASE == 0 || FORGE_DEPRECATION_CASE == 18
    {
        using env = domain_env<tagged_domain>;
        static_assert(std::same_as<ex::env_domain_t<env>, decltype(ex::get_domain(env{}))>);
    }
#endif
#if FORGE_DEPRECATION_CASE == 0 || FORGE_DEPRECATION_CASE == 19
    {
        using recv = domain_receiver;
        static_assert(std::same_as<ex::receiver_domain_t<recv>,
            decltype(ex::get_domain(ex::get_env(std::declval<const recv&>())))>);
    }
#endif
#if FORGE_DEPRECATION_CASE == 0 || FORGE_DEPRECATION_CASE == 20
    {
        using sndr = decltype(ex::just(42));
        static_assert(std::same_as<ex::sender_domain_t<sndr>,
            decltype(ex::get_domain(ex::get_env(std::declval<sndr>())))>);
    }
#endif
#if FORGE_DEPRECATION_CASE == 0 || FORGE_DEPRECATION_CASE == 21
    {
        using factory = ex::read_env_t;
        static_assert(std::same_as<factory, std::remove_cvref_t<decltype(ex::read_env)>>);
    }
#endif
#if FORGE_DEPRECATION_CASE == 0 || FORGE_DEPRECATION_CASE == 22
    {
        using sndr = decltype(ex::just(42));
        static_assert(std::same_as<ex::value_types_of<sndr, ex::env<>>::type, ex::value_types_of_t<sndr>>);
    }
#endif
#if FORGE_DEPRECATION_CASE == 0 || FORGE_DEPRECATION_CASE == 23
    {
        using sndr = decltype(ex::just_error(42));
        static_assert(std::same_as<ex::error_types_of<sndr, ex::env<>>::type, ex::error_types_of_t<sndr>>);
    }
#endif
#if FORGE_DEPRECATION_CASE == 0 || FORGE_DEPRECATION_CASE == 24
    {
        using sndr = decltype(ex::just_stopped());
        static_assert(ex::sends_stopped_v<sndr, ex::env<>> && ex::sends_stopped<sndr>);
    }
#endif
#if FORGE_DEPRECATION_CASE == 0 || FORGE_DEPRECATION_CASE == 25
    {
        constexpr progress_scheduler<false> legacy{};
        constexpr progress_scheduler<true> queried{};
        static_assert(ex::scheduler<decltype(legacy)> && ex::scheduler<decltype(queried)>);
        static_assert(ex::get_completion_scheduler<ex::set_value_t>(ex::get_env(ex::schedule(legacy))) == legacy);
        static_assert(ex::get_completion_scheduler<ex::set_value_t>(ex::get_env(ex::schedule(queried))) == queried);
        constexpr auto old = ex::get_forward_progress_guarantee(legacy);
        constexpr auto replacement = ex::get_forward_progress_guarantee(queried);
        static_assert(old == replacement && old == ex::forward_progress_guarantee::weakly_parallel);
    }
#endif
    return 0;
}

#if FORGE_DEPRECATION_CASE == 0 || defined(FORGE_SUPPRESS_DEPRECATION)
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
#endif
