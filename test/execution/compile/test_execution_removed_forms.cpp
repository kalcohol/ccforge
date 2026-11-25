#include "../scheduler_fixture.hpp"

#include "test_bulk.hpp"
#include <execution>

#include "../bulk_policy_probe.hpp"

#include <concepts>
#include <memory>
#include <type_traits>
#include <utility>

namespace ex = std::execution;

namespace {
using source = decltype(ex::just(42));
using scheduler = ex::inline_scheduler;
using environment = ex::env<>;

struct identity : ex::sender_adaptor_closure<identity> {
    template<ex::sender S>
    constexpr auto operator()(S&& s) const -> std::decay_t<S> {
        return std::forward<S>(s);
    }
};

struct callable {
    template<ex::sender S>
    auto operator()(S&& s) const -> std::decay_t<S> {
        return std::forward<S>(s);
    }
};

struct custom_pipe : callable {
    template<ex::sender S>
    friend auto operator|(S&& s, custom_pipe f) -> std::decay_t<S> {
        return f(std::forward<S>(s));
    }
};

template<class Left, class Right>
concept pipeable = requires(Left&& left, Right&& right) {
    std::forward<Left>(left) | std::forward<Right>(right);
};

using unstoppable = decltype(ex::unstoppable);
static_assert(!std::invocable<decltype(ex::write_env), const environment&>);
static_assert(std::invocable<decltype(ex::write_env), source, const environment&>);
static_assert(!std::invocable<unstoppable>);
static_assert(std::invocable<unstoppable, source>);
static_assert(!pipeable<source, unstoppable>);
static_assert(!pipeable<unstoppable, identity>);
static_assert(!pipeable<identity, unstoppable>);

static_assert(!std::invocable<decltype(ex::into_variant)>);
static_assert(std::invocable<decltype(ex::into_variant), source>);
static_assert(pipeable<source, decltype(ex::into_variant)>);
static_assert(!std::invocable<decltype(ex::stopped_as_optional)>);
static_assert(std::invocable<decltype(ex::stopped_as_optional), source>);
static_assert(pipeable<source, decltype(ex::stopped_as_optional)>);

template<class Closure>
constexpr bool rejects_nonclosure =
    !std::invocable<decltype(ex::on), const scheduler&, Closure> &&
    !std::invocable<decltype(ex::on), source, const scheduler&, Closure>;

static_assert(rejects_nonclosure<int>);
static_assert(rejects_nonclosure<custom_pipe>);
static_assert(rejects_nonclosure<callable>);
static_assert(rejects_nonclosure<callable&>);
static_assert(rejects_nonclosure<const callable&>);
static_assert(rejects_nonclosure<const callable>);
static_assert(std::invocable<decltype(ex::on), const scheduler&, source>);
static_assert(std::invocable<decltype(ex::on), source, const scheduler&, identity>);
static_assert(pipeable<source, decltype(ex::on(scheduler{}, identity{}))>);

using continues_on = decltype(ex::continues_on);
template<class Continuation, class S, class Sch>
concept piped_continuation = requires(Continuation c, S&& s, Sch&& sch) {
    std::forward<S>(s) | c(std::forward<Sch>(sch));
};

static_assert(!std::invocable<continues_on, scheduler>);
static_assert(!piped_continuation<continues_on, source, scheduler>);
static_assert(ex::sender<decltype(ex::continues_on(ex::just(42), scheduler{}))>);
static_assert(!std::invocable<decltype(ex::starts_on), scheduler>);
static_assert(ex::sender<decltype(ex::starts_on(scheduler{}, ex::just(42)))>);
static_assert(!std::invocable<decltype(ex::schedule_from), scheduler, source>);
static_assert(!pipeable<source, decltype(ex::schedule_from)>);
static_assert(ex::sender<decltype(ex::schedule_from(ex::just(42)))>);

template<bool Member, bool Legacy = false>
struct progress_scheduler {
    using scheduler_concept = ex::scheduler_tag;

    constexpr bool operator==(const progress_scheduler&) const noexcept = default;

    template<class... Env>
    constexpr auto query(ex::get_completion_scheduler_t<ex::set_value_t>, const Env&...) const noexcept
        -> progress_scheduler { return *this; }

    struct schedule_sender {
        using sender_concept = ex::sender_tag;

        template<class Self, class... Env>
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
        -> ex::forward_progress_guarantee requires Member {
        return ex::forward_progress_guarantee::weakly_parallel;
    }

    friend constexpr auto tag_invoke(ex::get_forward_progress_guarantee_t, const progress_scheduler&) noexcept
        -> ex::forward_progress_guarantee requires Legacy {
        return ex::forward_progress_guarantee::weakly_parallel;
    }
};

using progress_query = ex::get_forward_progress_guarantee_t;
static_assert(!std::invocable<progress_query, const progress_scheduler<false>&>);
static_assert(!std::invocable<progress_query, const progress_scheduler<false, true>&>);
static_assert(!ex::scheduler<progress_scheduler<false>>);
static_assert(!ex::scheduler<progress_scheduler<false, true>>);
static_assert(ex::scheduler<progress_scheduler<true>>);
constexpr progress_scheduler<true> queried_scheduler{};
static_assert(ex::get_forward_progress_guarantee(queried_scheduler) ==
              ex::forward_progress_guarantee::weakly_parallel);
static_assert(ex::get_completion_scheduler<ex::set_value_t>(ex::get_env(ex::schedule(queried_scheduler))) ==
              queried_scheduler);
static_assert(scheduler_fixture::agrees_with_schedule(queried_scheduler));
static_assert(scheduler_fixture::agrees_with_schedule(queried_scheduler, environment{}));

struct each_index {
    void operator()(int, int&) const noexcept {}
};
struct each_chunk {
    void operator()(int, int, int&) const noexcept {}
};

template<class Cpo, class Function>
concept explicit_serial_call = requires(Cpo cpo, Function f) {
    cpo.template operator()<source, const int, Function>(ex::just(42), 2, std::move(f));
};
template<class Cpo, class Function>
concept explicit_serial_bind = requires(Cpo cpo, Function f) {
    cpo.template operator()<const int, Function>(2, std::move(f));
};

template<class Standard, class Function>
constexpr bool rejects_serial_forms =
    !std::invocable<Standard, source, int, Function> &&
    !std::invocable<Standard, int, Function> &&
    !explicit_serial_call<Standard, Function> &&
    !explicit_serial_bind<Standard, Function>;

template<class Serial, class Function>
constexpr bool accepts_serial_forms =
    std::invocable<Serial, source, int, Function> &&
    pipeable<source, std::invoke_result_t<Serial, int, Function>> &&
    explicit_serial_call<Serial, Function> &&
    explicit_serial_bind<Serial, Function>;

static_assert(rejects_serial_forms<decltype(ex::bulk), each_index>);
static_assert(rejects_serial_forms<decltype(ex::bulk_chunked), each_chunk>);
static_assert(rejects_serial_forms<decltype(ex::bulk_unchunked), each_index>);
static_assert(accepts_serial_forms<decltype(test_bulk::bulk), each_index>);
static_assert(accepts_serial_forms<decltype(test_bulk::bulk_chunked), each_chunk>);
static_assert(accepts_serial_forms<decltype(test_bulk::bulk_unchunked), each_index>);

template<class Fn, class Cpo>
void check_const_shape(Cpo cpo) {
    auto direct = cpo.template operator()<source, const int, Fn>(ex::just(42), 2, Fn{});
    auto bound = cpo.template operator()<const int, Fn>(2, Fn{});
    static_assert(std::same_as<decltype(direct.__shape), const int>);
    static_assert(std::same_as<decltype(bound.__shape_), const int>);
}

#if FORGE_TEST_BULK_HAS_NATIVE_POLICIES
template<class Cpo, class Function>
constexpr bool has_policy_forms =
    std::invocable<Cpo, source, decltype(ex::seq), int, Function> &&
    pipeable<source, std::invoke_result_t<Cpo, decltype(ex::seq), int, Function>>;

static_assert(has_policy_forms<decltype(ex::bulk), each_index>);
static_assert(has_policy_forms<decltype(ex::bulk_chunked), each_chunk>);
static_assert(has_policy_forms<decltype(ex::bulk_unchunked), each_index>);
#endif

template<class Domain>
concept legacy_default_transform = requires(Domain domain, source& sender) {
    domain.transform_sender(sender, environment{});
};
static_assert(!legacy_default_transform<ex::default_domain>);
static_assert(std::same_as<decltype(ex::default_domain::transform_sender(
    ex::set_value, std::declval<source&>(), environment{})), source&>);

struct legacy_domain {
    template<ex::sender S, class E>
    S&& transform_sender(S&& sender, const E&) const {
        ++*sender.calls;
        return std::forward<S>(sender);
    }
};

struct tagged_domain {
    template<class Tag, ex::sender S, class E>
    S&& transform_sender(Tag, S&& sender, const E&) const noexcept {
        ++*sender.calls;
        return std::forward<S>(sender);
    }
};

template<class Domain>
struct transfer_source {
    using sender_concept = ex::sender_tag;
    int* calls;

    struct attrs {
        auto query(ex::get_completion_domain_t<ex::set_value_t>) const noexcept -> Domain { return {}; }
    };

    auto get_env() const noexcept -> attrs { return {}; }
};

using legacy_transform_env = ex::prop<ex::get_domain_t, legacy_domain>;
// [exec.snd.transform]/3-5 ignores the potentially throwing legacy hook.
static_assert(!noexcept(legacy_domain{}.transform_sender(
    std::declval<transfer_source<legacy_domain>&>(), std::declval<const legacy_transform_env&>())));
static_assert(noexcept(ex::transform_sender(
    std::declval<transfer_source<legacy_domain>&>(), std::declval<const legacy_transform_env&>())));

template<class Domain>
bool transforms_in_both_phases(int expected_calls) {
    int calls = 0;
    auto env = ex::prop{ex::get_domain, Domain{}};
    auto transformed = ex::transform_sender(transfer_source<Domain>{std::addressof(calls)}, env);
    return calls == expected_calls && transformed.calls == std::addressof(calls);
}
} // namespace

int main() {
    check_const_shape<each_index>(test_bulk::bulk);
    check_const_shape<each_chunk>(test_bulk::bulk_chunked);
    check_const_shape<each_index>(test_bulk::bulk_unchunked);
    source sndr = ex::just(42);
    const auto& identity_result = ex::default_domain::transform_sender(ex::set_value, sndr, environment{});
    // [exec.domain.default] selects the tagged customization or the default.
    return std::addressof(identity_result) == std::addressof(sndr) &&
           transforms_in_both_phases<legacy_domain>(0) &&
           transforms_in_both_phases<tagged_domain>(2) ? 0 : 1;
}
