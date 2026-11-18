// Include fragments directly so native stand-aside cannot hide regressions.
#include <forge/execution_extensions.hpp>
#include "cpp26/execution/inline_scheduler.hpp"
#include "cpp26/execution/run_loop.hpp"
#include "cpp26/execution/write_env.hpp"
#include "cpp26/execution/counting_scope.hpp"
#include "cpp26/execution/spawn_future.hpp"
#include "cpp26/execution/continues_on.hpp"
#include <gtest/gtest.h>

#include "../scheduler_fixture.hpp"

#include <algorithm>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace ex = std::execution;
namespace {
struct forwarding_key {
    constexpr bool query(std::forwarding_query_t) const noexcept { return true; }
};
struct local_key {};
struct probe_env {
    int forwarded;
    int local;
    int query(forwarding_key) const noexcept { return forwarded; }
    int query(local_key) const noexcept { return local; }
};
template<class E>
concept has_local = requires(const E& e) { e.query(local_key{}); };

template<bool OverlayLocal>
struct probe_sender {
    using sender_concept = ex::sender_tag;
    template<class Self, class Env>
    static auto get_completion_signatures() noexcept {
        static_assert(has_local<Env> == OverlayLocal);
        return ex::completion_signatures<ex::set_value_t(int)>{};
    }
    template<class R>
    struct op {
        using operation_state_concept = ex::operation_state_tag;
        R receiver;
        void start() & noexcept {
            auto e = ex::get_env(receiver);
            static_assert(has_local<decltype(e)> == OverlayLocal);
            int value = e.query(forwarding_key{});
            if constexpr (OverlayLocal) {
                value += e.query(local_key{});
            }
            ex::set_value(std::move(receiver), value);
        }
    };
    template<ex::receiver R>
    auto connect(R r) const noexcept -> op<R> { return {std::move(r)}; }
};
struct probe_receiver {
    using receiver_concept = ex::receiver_tag;
    int* result;
    void set_value(int value) && noexcept { *result = value; }
    probe_env get_env() const noexcept { return {10, 20}; }
};

struct transfer_child_domain {};
struct transfer_destination_domain {};
struct transfer_child_attrs {
    ex::run_loop::scheduler scheduler;
    template<class Tag>
    auto query(ex::get_completion_scheduler_t<Tag>) const noexcept { return scheduler; }
    template<class Tag, class... Envs>
    transfer_child_domain query(ex::get_completion_domain_t<Tag>, Envs&&...) const noexcept {
        return {};
    }
};
struct transfer_schedule_attrs {
    template<class Tag, class... Envs>
    auto query(ex::get_completion_domain_t<Tag>, Envs&&...) const
        noexcept(!(has_local<Envs> || ...))
        -> std::conditional_t<(has_local<Envs> || ...), transfer_child_domain,
                              transfer_destination_domain> { return {}; }
};
struct transfer_schedule_sender {
    using sender_concept = ex::sender_tag;
    int* connections;
    int* active_resource;
    int destination;
    transfer_schedule_attrs get_env() const noexcept { return {}; }
    template<class Self, class Env>
    static auto get_completion_signatures() noexcept {
        static_assert(!has_local<Env>);
        static_assert(std::same_as<decltype(ex::get_completion_domain<ex::set_value_t>(
            transfer_schedule_attrs{}, std::declval<Env>())), transfer_destination_domain>);
        return ex::completion_signatures<ex::set_value_t()>{};
    }
    template<class R>
    struct op {
        using operation_state_concept = ex::operation_state_tag;
        R receiver;
        int* active_resource;
        int destination;
        void start() & noexcept {
            auto env = ex::get_env(receiver);
            static_assert(!has_local<decltype(env)>);
            static_assert(std::same_as<decltype(ex::get_completion_domain<ex::set_value_t>(
                transfer_schedule_attrs{}, env)), transfer_destination_domain>);
            EXPECT_EQ(env.query(forwarding_key{}), 10);
            *active_resource = destination;
            ex::set_value(std::move(receiver));
        }
    };
    template<ex::receiver R>
    auto connect(R r) const noexcept -> op<R> {
        auto env = ex::get_env(r);
        static_assert(!has_local<decltype(env)>);
        static_assert(std::same_as<decltype(ex::get_completion_domain<ex::set_value_t>(
            transfer_schedule_attrs{}, env)), transfer_destination_domain>);
        EXPECT_EQ(env.query(forwarding_key{}), 10);
        ++*connections;
        return {std::move(r), active_resource, destination};
    }
};
struct transfer_scheduler {
    using scheduler_concept = ex::scheduler_tag;
    constexpr auto query(ex::get_forward_progress_guarantee_t) const noexcept
        -> ex::forward_progress_guarantee {
        return ex::forward_progress_guarantee::weakly_parallel;
    }

    int* connections;
    int* active_resource;
    int destination;
    auto schedule() const noexcept {
        return transfer_schedule_sender{connections, active_resource, destination};
    }
    transfer_destination_domain query(ex::get_domain_t) const noexcept { return {}; }
    bool operator==(const transfer_scheduler&) const noexcept = default;
};
struct transfer_receiver {
    using receiver_concept = ex::receiver_tag;
    probe_env environment;
    int* active_resource;
    int destination;
    bool* matched;
    void set_value(bool value) && noexcept {
        *matched = value && *active_resource == destination;
    }
    void set_error(std::exception_ptr) && noexcept { *matched = false; }
    void set_stopped() && noexcept { *matched = false; }
    const probe_env& get_env() const noexcept { return environment; }
};

struct domain {};
struct domain_env {
    ex::run_loop::scheduler scheduler;
    auto query(ex::get_scheduler_t) const noexcept { return scheduler; }
    auto query(ex::get_domain_t) const noexcept -> domain { return {}; }
};

struct domain_only_env {
    ex::run_loop::scheduler scheduler;
    auto query(ex::get_start_scheduler_t) const noexcept { return scheduler; }
    auto query(ex::get_domain_t) const noexcept -> domain { return {}; }
};
struct missing_scheduler_receiver {
    using receiver_concept = ex::receiver_tag;
    domain_only_env environment;
    bool* matched;
    void set_value(ex::inline_scheduler scheduler) && noexcept {
        *matched = scheduler == ex::inline_scheduler{};
    }
    void set_error(std::exception_ptr) && noexcept { *matched = false; }
    domain_only_env get_env() const noexcept { return environment; }
};

// A conforming scheduler with a direct domain answer: every value category
// gives the nonthrowing prvalue that the public get_domain reports
// ([exec.get.domain]/2, [exec.queryable.general]/2). The public controls use it.
struct rvalue_domain {};
struct rvalue_domain_scheduler {
    using scheduler_concept = ex::scheduler_tag;
    constexpr auto schedule() const noexcept -> scheduler_fixture::schedule_sender<rvalue_domain_scheduler> {
        return {*this};
    }
    constexpr auto query(ex::get_forward_progress_guarantee_t) const noexcept {
        return ex::forward_progress_guarantee::weakly_parallel;
    }
    constexpr auto query(ex::get_domain_t) && noexcept -> rvalue_domain { return {}; }
    constexpr auto query(ex::get_domain_t) const& noexcept -> rvalue_domain { return {}; }
    bool operator==(const rvalue_domain_scheduler&) const noexcept = default;
};
static_assert(ex::scheduler<rvalue_domain_scheduler>);
static_assert(scheduler_fixture::agrees_with_schedule(rvalue_domain_scheduler{}));
static_assert(scheduler_fixture::agrees_with_schedule(rvalue_domain_scheduler{}, forge::empty_env{}));
static_assert(std::same_as<decltype(ex::get_domain(rvalue_domain_scheduler{})), rvalue_domain>);
static_assert(std::same_as<decltype(ex::get_domain(
    std::declval<const rvalue_domain_scheduler&>())), rvalue_domain>);
static_assert(noexcept(rvalue_domain_scheduler{}.query(ex::get_domain)) &&
              noexcept(std::declval<const rvalue_domain_scheduler&>().query(ex::get_domain)));

// Implementation fixtures. They are used only on __scheduler_env itself, to
// observe that it queries the stored scheduler as a const lvalue and where
// the answer lives; they are not offered as conforming schedulers.
struct raw_category_scheduler {
    using scheduler_concept = ex::scheduler_tag;
    auto schedule() const noexcept { return ex::just(); }
    auto query(ex::get_forward_progress_guarantee_t) const noexcept {
        return ex::forward_progress_guarantee::weakly_parallel;
    }
    // The exception specification tells the selected overload apart.
    auto query(ex::get_domain_t) && noexcept -> rvalue_domain { return {}; }
    auto query(ex::get_domain_t) const& noexcept(false) -> rvalue_domain { return {}; }
    bool operator==(const raw_category_scheduler&) const noexcept = default;
};
struct stored_domain {};
struct borrowing_domain_scheduler {
    using scheduler_concept = ex::scheduler_tag;
    stored_domain domain_tag;
    auto schedule() const noexcept { return ex::just(); }
    auto query(ex::get_forward_progress_guarantee_t) const noexcept {
        return ex::forward_progress_guarantee::weakly_parallel;
    }
    auto query(ex::get_domain_t) && noexcept -> const stored_domain& { return domain_tag; }
    auto query(ex::get_domain_t) const& noexcept -> const stored_domain& { return domain_tag; }
    bool operator==(const borrowing_domain_scheduler&) const noexcept { return true; }
};

// A schedule sender whose attributes are a borrowed, noncopyable object with
// static storage duration.
struct borrowed_attrs_domain {};
struct borrowed_attrs_scheduler;
struct noncopyable_schedule_attrs {
    noncopyable_schedule_attrs() = default;
    noncopyable_schedule_attrs(const noncopyable_schedule_attrs&) = delete;
    // With an environment the scheduler itself is the answer on the scheduler
    // side ([exec.get.compl.sched]/5.2), so the attributes give it too.
    template<class Env>
    auto query(ex::get_completion_scheduler_t<ex::set_value_t>, const Env&) const noexcept
        -> borrowed_attrs_scheduler;
    template<class... Envs>
    auto query(ex::get_completion_domain_t<ex::set_value_t>, Envs&&...) const noexcept
        -> borrowed_attrs_domain { return {}; }
};
inline const noncopyable_schedule_attrs shared_schedule_attrs;
struct borrowed_attrs_sender {
    using sender_concept = ex::sender_tag;
    template<class Self, class... Env>
    static constexpr auto get_completion_signatures() noexcept
        -> ex::completion_signatures<ex::set_value_t()> { return {}; }
    auto get_env() const noexcept -> const noncopyable_schedule_attrs& {
        return shared_schedule_attrs;
    }
    template<ex::receiver R>
    auto connect(R receiver) const
        noexcept(noexcept(ex::connect(ex::just(), std::move(receiver))))
        -> ex::connect_result_t<decltype(ex::just()), R> {
        return ex::connect(ex::just(), std::move(receiver));
    }
};
struct borrowed_attrs_scheduler {
    using scheduler_concept = ex::scheduler_tag;
    auto schedule() const noexcept -> borrowed_attrs_sender { return {}; }
    auto query(ex::get_forward_progress_guarantee_t) const noexcept {
        return ex::forward_progress_guarantee::weakly_parallel;
    }
    template<class... Envs>
    auto query(ex::get_completion_domain_t<ex::set_value_t>, Envs&&...) const noexcept
        -> borrowed_attrs_domain { return {}; }
    bool operator==(const borrowed_attrs_scheduler&) const noexcept = default;
};
template<class Env>
auto noncopyable_schedule_attrs::query(
    ex::get_completion_scheduler_t<ex::set_value_t>, const Env&) const noexcept
    -> borrowed_attrs_scheduler { return {}; }
static_assert(ex::scheduler<borrowed_attrs_scheduler>);

#if defined(__cpp_explicit_this_parameter) && __cpp_explicit_this_parameter >= 202110L
// A domain member that takes the scheduler by value, on a scheduler whose
// nonthrowing move leaves an observable mark on its source.
struct state_domain {
    int state = 0;
};
struct consumable_scheduler {
    int state = 7;
    consumable_scheduler() = default;
    consumable_scheduler(const consumable_scheduler&) = default;
    consumable_scheduler(consumable_scheduler&& other) noexcept : state(other.state) {
        other.state = -1;
    }
    auto query(this consumable_scheduler self, ex::get_domain_t) noexcept -> state_domain {
        return {self.state};
    }
};
#endif

// A predecessor whose attributes publish the scheduler as a const lvalue.
struct lvalue_scheduler_attrs {
    rvalue_domain_scheduler scheduler;
    auto query(ex::get_completion_scheduler_t<ex::set_value_t>) const noexcept
        -> const rvalue_domain_scheduler& { return scheduler; }
};
struct lvalue_scheduler_sender {
    using sender_concept = ex::sender_tag;
    template<class Self, class... Env>
    static constexpr auto get_completion_signatures() noexcept
        -> ex::completion_signatures<ex::set_value_t()> { return {}; }
    auto get_env() const noexcept -> lvalue_scheduler_attrs { return {}; }
    template<ex::receiver R>
    auto connect(R receiver) const
        noexcept(noexcept(ex::connect(ex::just(), std::move(receiver))))
        -> ex::connect_result_t<decltype(ex::just()), R> {
        return ex::connect(ex::just(), std::move(receiver));
    }
};

// [exec.sched]/3: a scheduler copy does not exit via an exception, whatever
// its exception specification says.
struct throwing_copy_scheduler {
    using scheduler_concept = ex::scheduler_tag;
    int resource = 0;
    explicit throwing_copy_scheduler(int id) noexcept : resource(id) {}
    throwing_copy_scheduler(const throwing_copy_scheduler& other) noexcept(false)
        : resource(other.resource) {}
    throwing_copy_scheduler& operator=(const throwing_copy_scheduler& other) noexcept(false) {
        resource = other.resource;
        return *this;
    }
    auto schedule() const noexcept -> scheduler_fixture::schedule_sender<throwing_copy_scheduler> {
        return {*this};
    }
    auto query(ex::get_forward_progress_guarantee_t) const noexcept {
        return ex::forward_progress_guarantee::weakly_parallel;
    }
    bool operator==(const throwing_copy_scheduler&) const noexcept = default;
};
static_assert(ex::scheduler<throwing_copy_scheduler>);
static_assert(!std::is_nothrow_copy_constructible_v<throwing_copy_scheduler>);

template<class Value>
struct let_env_receiver {
    using receiver_concept = ex::receiver_tag;
    domain_only_env environment;
    std::optional<Value>* result;
    void set_value(Value&& value) && noexcept { result->emplace(std::move(value)); }
    void set_error(std::exception_ptr) && noexcept {}
    void set_stopped() && noexcept {}
    domain_only_env get_env() const noexcept { return environment; }
};

template<class Env>
concept answers_domain = requires(const Env& env) { env.query(ex::get_domain); };

template<class Value, class Sender>
auto run_in_let_env(Sender sender, ex::run_loop::scheduler scheduler) {
    static_assert(std::same_as<
        ex::value_types_of_t<Sender, domain_only_env, std::type_identity_t, std::type_identity_t>,
        Value>);
    std::optional<Value> result;
    auto operation = ex::connect(std::move(sender),
        let_env_receiver<Value>{domain_only_env{scheduler}, &result});
    ex::start(operation);
    return result;
}

template<class Sender, class Scheduler>
void check_schedule_attributes(const Sender& sender, const Scheduler& scheduler) {
    auto attrs = ex::get_env(sender);
    EXPECT_EQ(ex::get_completion_scheduler<ex::set_value_t>(attrs), scheduler);
    EXPECT_EQ(ex::get_completion_scheduler<ex::set_value_t>(scheduler), scheduler);
}

struct stop_env {
    std::inplace_stop_token token;
    int* queries;
    auto query(std::get_stop_token_t) const noexcept {
        ++*queries;
        return token;
    }
};
struct stop_receiver {
    using receiver_concept = ex::receiver_tag;
    stop_env environment;
    bool* stopped;
    void set_value() && noexcept { *stopped = false; }
    void set_stopped() && noexcept { *stopped = true; }
    stop_env get_env() const noexcept { return environment; }
};

struct let_receiver_env {
    ex::run_loop::scheduler scheduler;
    auto query(ex::get_scheduler_t) const noexcept -> const ex::run_loop::scheduler& {
        return scheduler;
    }
    auto query(ex::get_start_scheduler_t) const noexcept { return ex::inline_scheduler{}; }
    int query(local_key) const noexcept { return 42; }
};

template<class Tag>
struct unary_let_attrs {
    ex::run_loop::scheduler scheduler;
    auto query(ex::get_completion_scheduler_t<Tag>) const noexcept
        -> const ex::run_loop::scheduler& { return scheduler; }
    // A scheduler answer must take precedence over a direct domain answer.
    auto query(ex::get_completion_domain_t<Tag>) const noexcept -> domain { return {}; }
};

template<class Tag>
struct dependent_let_attrs : unary_let_attrs<Tag> {
    using unary_let_attrs<Tag>::query;
    bool* filtered;
    int* queries;
    template<class Env>
    auto query(ex::get_completion_scheduler_t<Tag>, Env&& env) const
        noexcept(noexcept(ex::get_scheduler(static_cast<Env&&>(env))))
        -> decltype(ex::get_scheduler(static_cast<Env&&>(env))) {
        *filtered = !has_local<Env>;
        ++*queries;
        return ex::get_scheduler(static_cast<Env&&>(env));
    }
};

template<class Tag, class Attrs>
struct let_predecessor {
    using sender_concept = ex::sender_tag;
    Attrs attrs;
    // Returning attributes by value exercises a query result referring into
    // the temporary attributes, which let must copy before returning.
    Attrs get_env() const noexcept { return attrs; }
    template<class Self, class Env>
    static auto get_completion_signatures() noexcept {
        if constexpr (std::same_as<Tag, ex::set_error_t>) {
            return ex::completion_signatures<ex::set_error_t(int)>{};
        } else {
            return ex::completion_signatures<Tag()>{};
        }
    }
    template<class R>
    struct op {
        using operation_state_concept = ex::operation_state_tag;
        R receiver;
        void start() & noexcept {
            if constexpr (std::same_as<Tag, ex::set_error_t>) {
                ex::set_error(std::move(receiver), 7);
            } else {
                Tag{}(std::move(receiver));
            }
        }
    };
    template<ex::receiver R>
    auto connect(R r) const noexcept -> op<R> { return {std::move(r)}; }
};

template<class Tag>
void check_continues_destination() {
    ex::run_loop source_loop;
    int connections = 0;
    int active_resource = 0;
    transfer_scheduler destination{&connections, &active_resource, 2};
    auto source = let_predecessor<Tag, transfer_child_attrs>{{source_loop.get_scheduler()}};
    auto transfer = ex::continues_on(std::move(source), destination);
    auto attrs = ex::get_env(transfer);
    EXPECT_EQ(attrs.query(ex::get_completion_scheduler<Tag>), destination);
    static_assert(std::same_as<decltype(ex::get_completion_domain<Tag>(
        attrs, probe_env{})), transfer_destination_domain>);
    (void)attrs.query(ex::get_completion_domain<>, probe_env{});
    auto child = [destination, &active_resource](auto&...) {
        return ex::read_env([](const auto& env) noexcept {
            static_assert(!has_local<std::remove_cvref_t<decltype(env)>>);
            if constexpr (!std::same_as<Tag, ex::set_error_t>) {
                static_assert(std::same_as<decltype(ex::get_domain(env)),
                                           transfer_destination_domain>);
                (void)ex::get_domain(env);
                return std::pair{env.query(forwarding_key{}), ex::get_start_scheduler(env)};
            } else {
                return env.query(forwarding_key{});
            }
        }) | ex::then([destination, &active_resource](auto observed) noexcept {
            bool matched = active_resource == destination.destination;
            if constexpr (!std::same_as<Tag, ex::set_error_t>) {
                return matched && observed.first == 10 && observed.second == destination;
            } else {
                return matched && observed == 10;
            }
        });
    };
    auto sender = [&transfer, &child] {
        if constexpr (std::same_as<Tag, ex::set_value_t>) {
            return ex::let_value(std::move(transfer), child);
        } else if constexpr (std::same_as<Tag, ex::set_error_t>) {
            return ex::let_error(std::move(transfer), child);
        } else {
            return ex::let_stopped(std::move(transfer), child);
        }
    }();
    bool matched = false;
    auto operation = ex::connect(std::move(sender),
        transfer_receiver{{10, 20}, &active_resource, 2, &matched});
    EXPECT_EQ(connections, 1);
    EXPECT_EQ(active_resource, 0);
    ex::start(operation);
    EXPECT_TRUE(matched);
    EXPECT_EQ(connections, 1);
}

template<class Tag, class Attrs>
auto make_scheduler_let(Attrs attrs) {
    auto child = [](auto&...) { return ex::read_env(ex::get_start_scheduler); };
    auto predecessor = let_predecessor<Tag, Attrs>{std::move(attrs)};
    if constexpr (std::same_as<Tag, ex::set_value_t>) {
        return ex::let_value(std::move(predecessor), child);
    } else if constexpr (std::same_as<Tag, ex::set_error_t>) {
        return ex::let_error(std::move(predecessor), child);
    } else {
        return ex::let_stopped(std::move(predecessor), child);
    }
}

template<bool Borrow>
struct let_receiver {
    using receiver_concept = ex::receiver_tag;
    const let_receiver_env* environment;
    ex::run_loop::scheduler expected;
    bool* matched;
    void set_value(ex::run_loop::scheduler scheduler) && noexcept { *matched = scheduler == expected; }
    void set_error(std::exception_ptr) && noexcept { *matched = false; }
    void set_stopped() && noexcept { *matched = false; }
    decltype(auto) get_env() const noexcept {
        if constexpr (Borrow) return (*environment);
        else return let_receiver_env{*environment};
    }
};

template<bool Borrow, class Tag, class Attrs>
void check_let_scheduler(Attrs attrs, ex::run_loop::scheduler outer,
                         ex::run_loop::scheduler expected) {
    ex::run_loop changed_loop;
    let_receiver_env environment{outer};
    bool matched = false;
    auto sender = make_scheduler_let<Tag>(std::move(attrs));
    using env_t = ex::env_of_t<let_receiver<Borrow>>;
    static_assert(std::same_as<ex::value_types_of_t<decltype(sender), env_t>,
                               std::variant<std::tuple<ex::run_loop::scheduler>>>);
    auto operation = ex::connect(std::move(sender),
        let_receiver<Borrow>{&environment, expected, &matched});
    // A reference query answer must be captured as a value at connect time.
    // This also exercises references into a receiver environment returned by value.
    environment.scheduler = changed_loop.get_scheduler();
    ex::start(operation);
    EXPECT_TRUE(matched);
}

template<class Tag>
void check_unary_let_scheduler() {
    ex::run_loop predecessor_loop;
    ex::run_loop receiver_loop;
    auto attrs = unary_let_attrs<Tag>{predecessor_loop.get_scheduler()};
    check_let_scheduler<false, Tag>(attrs, receiver_loop.get_scheduler(), attrs.scheduler);
    check_let_scheduler<true, Tag>(attrs, receiver_loop.get_scheduler(), attrs.scheduler);
}

template<class Tag>
void check_dependent_let_scheduler() {
    ex::run_loop predecessor_loop;
    ex::run_loop receiver_loop;
    bool filtered = false;
    int queries = 0;
    auto attrs = dependent_let_attrs<Tag>{{predecessor_loop.get_scheduler()}, &filtered, &queries};
    check_let_scheduler<false, Tag>(attrs, receiver_loop.get_scheduler(), receiver_loop.get_scheduler());
    EXPECT_TRUE(filtered);
    EXPECT_EQ(queries, 1);
    filtered = false;
    check_let_scheduler<true, Tag>(attrs, receiver_loop.get_scheduler(), receiver_loop.get_scheduler());
    EXPECT_TRUE(filtered);
    EXPECT_EQ(queries, 2);
}
} // namespace

TEST(SchedulerEnvQueriesTest, WriteEnvFiltersLocalBaseAndKeepsLocalOverlay) {
    int value = -1;
    auto base_sender = ex::write_env(probe_sender<false>{}, forge::empty_env{});
    using base_signatures = ex::completion_signatures_of_t<decltype(base_sender), probe_env>;
    static_assert(std::same_as<base_signatures, ex::completion_signatures<ex::set_value_t(int)>>);
    auto base = ex::connect(std::move(base_sender), probe_receiver{&value});
    ex::start(base);
    EXPECT_EQ(value, 10);

    auto overlay_sender = ex::write_env(probe_sender<true>{}, probe_env{30, 40});
    using overlay_signatures = ex::completion_signatures_of_t<decltype(overlay_sender), probe_env>;
    static_assert(std::same_as<overlay_signatures, ex::completion_signatures<ex::set_value_t(int)>>);
    auto overlay = ex::connect(std::move(overlay_sender), probe_receiver{&value});
    ex::start(overlay);
    EXPECT_EQ(value, 70);
}

TEST(SchedulerEnvQueriesTest, InlineSchedulerUsesTheSuppliedResourceAndDomain) {
    ex::run_loop first;
    ex::run_loop second;
    ex::inline_scheduler scheduler;
    auto attrs = ex::get_env(ex::schedule(scheduler));
    domain_env environment{first.get_scheduler()};
    EXPECT_EQ(scheduler.query(ex::get_completion_scheduler<ex::set_value_t>, environment),
              first.get_scheduler());
    EXPECT_EQ(attrs.query(ex::get_completion_scheduler<ex::set_value_t>, environment),
              first.get_scheduler());
    environment.scheduler = second.get_scheduler();
    EXPECT_EQ(ex::get_completion_scheduler<ex::set_value_t>(scheduler, environment),
              second.get_scheduler());
    EXPECT_EQ(ex::get_completion_scheduler<ex::set_value_t>(attrs, environment),
              second.get_scheduler());
    static_assert(std::same_as<decltype(ex::get_completion_domain<ex::set_value_t>(
                                   scheduler, environment)), domain>);
    static_assert(std::same_as<decltype(ex::get_completion_domain<ex::set_value_t>(
                                   attrs, environment)), domain>);
    (void)attrs.query(ex::get_completion_domain<ex::set_value_t>, environment);
    (void)scheduler.query(ex::get_completion_domain<ex::set_value_t>, environment);
}

TEST(SchedulerEnvQueriesTest, ContinuesOnFiltersBothReceiverEnvironments) {
    int connections = 0;
    int active_resource = 0;
    int result = 0;
    auto sender = ex::continues_on(probe_sender<false>{},
        transfer_scheduler{&connections, &active_resource, 2});
    using signatures = ex::completion_signatures_of_t<decltype(sender), probe_env>;
    static_assert(std::same_as<signatures, ex::completion_signatures<ex::set_value_t(int)>>);
    auto operation = ex::connect(std::move(sender), probe_receiver{&result});
    EXPECT_EQ(connections, 1);
    EXPECT_EQ(active_resource, 0);
    ex::start(operation);
    EXPECT_EQ(result, 10);
    EXPECT_EQ(active_resource, 2);
}

TEST(SchedulerEnvQueriesTest, ContinuesOnDomainUsesScheduleReceiverEnvironment) {
    int connections = 0;
    int active_resource = 0;
    int result = 0;
    auto sender = ex::continues_on(probe_sender<false>{},
        transfer_scheduler{&connections, &active_resource, 2});
    auto attrs = ex::get_env(sender);
    probe_env environment{10, 20};
    using query = ex::get_completion_domain_t<ex::set_value_t>;
    static_assert(std::same_as<decltype(transfer_schedule_attrs{}.query(query{}, environment)),
                               transfer_child_domain>);
    static_assert(std::same_as<decltype(attrs.query(query{}, environment)),
                               transfer_destination_domain>);
    static_assert(noexcept(attrs.query(query{}, environment)));
    static_assert(std::same_as<decltype(ex::get_completion_domain<ex::set_value_t>(
        attrs, environment)), transfer_destination_domain>);
    (void)attrs.query(query{}, environment);
    auto operation = ex::connect(std::move(sender), probe_receiver{&result});
    ex::start(operation);
    EXPECT_EQ(connections, 1);
    EXPECT_EQ(result, 10);
    EXPECT_EQ(active_resource, 2);
}

TEST(SchedulerEnvQueriesTest, ContinuesOnValuePublishesDestinationToLet) {
    check_continues_destination<ex::set_value_t>();
}

TEST(SchedulerEnvQueriesTest, ContinuesOnStoppedPublishesDestinationToLet) {
    check_continues_destination<ex::set_stopped_t>();
}

TEST(SchedulerEnvQueriesTest, ContinuesOnErrorReachesLetAfterDestinationHop) {
    check_continues_destination<ex::set_error_t>();
}

TEST(SchedulerEnvQueriesTest, RunLoopSchedulerAndAttributesHaveValueAndStoppedParity) {
    ex::run_loop first;
    ex::run_loop second;
    auto scheduler = first.get_scheduler();
    auto sender = ex::schedule(scheduler);
    check_schedule_attributes(sender, scheduler);
    auto attrs = ex::get_env(sender);
    EXPECT_EQ(ex::get_completion_scheduler<ex::set_stopped_t>(scheduler), scheduler);
    EXPECT_EQ(ex::get_completion_scheduler<ex::set_stopped_t>(attrs), scheduler);
    EXPECT_NE(ex::get_completion_scheduler<ex::set_value_t>(scheduler), second.get_scheduler());
    // TRY-QUERY falls back to unary queries when environment arguments are supplied.
    EXPECT_EQ(ex::get_completion_scheduler<ex::set_value_t>(scheduler, forge::empty_env{}),
              ex::get_completion_scheduler<ex::set_value_t>(attrs, forge::empty_env{}));
    EXPECT_EQ(ex::get_completion_scheduler<ex::set_stopped_t>(scheduler, forge::empty_env{}),
              ex::get_completion_scheduler<ex::set_stopped_t>(attrs, forge::empty_env{}));
}

TEST(SchedulerEnvQueriesTest, InlineScheduleLetAndWriteEnvReachTheChildEnvironment) {
    ex::run_loop loop;
    auto scheduler = loop.get_scheduler();
    auto sender = ex::write_env(
        ex::schedule(ex::inline_scheduler{}) | ex::let_value([] {
            return ex::read_env(ex::get_scheduler);
        }), ex::prop{ex::get_scheduler, scheduler});
    auto result = forge::sync_wait(std::move(sender));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), scheduler);
}

TEST(SchedulerEnvQueriesTest, InlineLetWithoutReceiverSchedulerUsesInlineScheduler) {
    ex::run_loop loop;
    domain_only_env environment{loop.get_scheduler()};
    auto predecessor = ex::schedule(ex::inline_scheduler{});
    bool unused = false;
    missing_scheduler_receiver probe{environment, &unused};
    auto selected = ex::__forge_let::__make_let_env<ex::__forge_let::__value_tag>(
        predecessor, probe);
    static_assert(std::same_as<decltype(selected),
                               ex::__forge_let::__scheduler_env<ex::inline_scheduler>>);
    EXPECT_EQ(ex::get_start_scheduler(selected), ex::inline_scheduler{});
    auto sender = ex::let_value(predecessor, [] {
        return ex::read_env(ex::get_start_scheduler);
    });
    bool matched = false;
    auto operation = ex::connect(std::move(sender), missing_scheduler_receiver{environment, &matched});
    ex::start(operation);
    EXPECT_TRUE(matched);
}

TEST(SchedulerEnvQueriesTest, LetEnvQueriesTheDomainOfASelectedPrvalueScheduler) {
    ex::run_loop loop;
    auto sender = ex::let_value(ex::schedule(rvalue_domain_scheduler{}), [] {
        return ex::read_env(ex::get_domain);
    });
    using selected = ex::__forge_let::__let_env_t<ex::__forge_let::__value_tag,
        decltype(ex::schedule(rvalue_domain_scheduler{})), domain_only_env>;
    static_assert(std::same_as<selected, ex::__forge_let::__scheduler_env<rvalue_domain_scheduler>>);
    static_assert(answers_domain<selected>);
    // The scheduler's own domain, not the receiver's ([exec.snd.expos]/10).
    EXPECT_TRUE(run_in_let_env<rvalue_domain>(std::move(sender), loop.get_scheduler()).has_value());

    auto started = ex::starts_on(rvalue_domain_scheduler{}, ex::read_env(ex::get_domain));
    EXPECT_TRUE(run_in_let_env<rvalue_domain>(std::move(started), loop.get_scheduler()).has_value());
}

TEST(SchedulerEnvQueriesTest, LetEnvQueriesTheDomainOfASelectedLvalueScheduler) {
    ex::run_loop loop;
    using selected = ex::__forge_let::__let_env_t<ex::__forge_let::__value_tag,
        lvalue_scheduler_sender, domain_only_env>;
    static_assert(std::same_as<selected,
        ex::__forge_let::__scheduler_env<rvalue_domain_scheduler>>);
    static_assert(answers_domain<selected>);
    auto sender = ex::let_value(lvalue_scheduler_sender{}, [] {
        return ex::read_env(ex::get_domain);
    });
    EXPECT_TRUE(run_in_let_env<rvalue_domain>(std::move(sender), loop.get_scheduler()).has_value());
    auto start = ex::let_value(lvalue_scheduler_sender{}, [] {
        return ex::read_env(ex::get_start_scheduler);
    });
    static_assert(std::same_as<decltype(std::declval<const selected&>().query(
        ex::get_start_scheduler)), rvalue_domain_scheduler>);
    EXPECT_EQ(run_in_let_env<rvalue_domain_scheduler>(std::move(start), loop.get_scheduler()),
              rvalue_domain_scheduler{});
}

// Implementation test: __scheduler_env queries the stored scheduler as a
// const lvalue, whatever the category of the selected scheduler expression.
TEST(SchedulerEnvQueriesTest, SchedulerEnvQueriesTheDomainOnAConstLvalue) {
    using selected = ex::__forge_let::__scheduler_env<raw_category_scheduler>;
    static_assert(answers_domain<selected>);
    static_assert(!noexcept(std::declval<const selected&>().query(ex::get_domain)));
    selected environment{raw_category_scheduler{}};
    (void)environment.query(ex::get_domain);
    SUCCEED();
}

// Implementation test: a domain answer that refers into the scheduler refers
// into the scheduler that the environment owns ([exec.queryable.general]/6).
TEST(SchedulerEnvQueriesTest, SchedulerEnvKeepsStorageForABorrowingDomainAnswer) {
    using selected = ex::__forge_let::__scheduler_env<borrowing_domain_scheduler>;
    selected environment{borrowing_domain_scheduler{}};
    const stored_domain& answer = environment.query(ex::get_domain);
    EXPECT_EQ(std::addressof(answer), std::addressof(environment.__scheduler.domain_tag));
    EXPECT_EQ(std::addressof(answer), std::addressof(environment.query(ex::get_domain)));
}

#if defined(__cpp_explicit_this_parameter) && __cpp_explicit_this_parameter >= 202110L
// Implementation test: a member that takes the scheduler by value receives an
// intact scheduler on every query, and the stored one is never moved from.
TEST(SchedulerEnvQueriesTest, SchedulerEnvRepeatsAConsumingDomainQuery) {
    using selected = ex::__forge_let::__scheduler_env<consumable_scheduler>;
    const selected environment{consumable_scheduler{}};
    EXPECT_EQ(environment.query(ex::get_domain).state, 7);
    EXPECT_EQ(environment.query(ex::get_domain).state, 7);
    EXPECT_EQ(environment.query(ex::get_start_scheduler).state, 7);
    EXPECT_EQ(environment.__scheduler.state, 7);
}
#endif

// The completion-domain member of continues_on's attributes queries the
// attributes of schedule(sch) without copying them ([exec.sched]/6).
TEST(SchedulerEnvQueriesTest, ContinuesOnQueriesBorrowedNoncopyableScheduleAttributes) {
    ex::run_loop loop;
    domain_only_env environment{loop.get_scheduler()};
    auto sender = ex::continues_on(ex::just(), borrowed_attrs_scheduler{});
    auto attrs = ex::get_env(sender);
    using query = ex::get_completion_domain_t<ex::set_value_t>;
    static_assert(std::same_as<decltype(attrs.query(query{}, environment)), borrowed_attrs_domain>);
    static_assert(noexcept(attrs.query(query{}, environment)));
    static_assert(std::same_as<decltype(ex::get_completion_domain<ex::set_value_t>(
        attrs, environment)), borrowed_attrs_domain>);
    static_assert(noexcept(ex::get_completion_domain<ex::set_value_t>(attrs, environment)));
    // [exec.sched]/5-6 for the fixture, in the environment of this test.
    EXPECT_TRUE(scheduler_fixture::agrees_with_schedule(borrowed_attrs_scheduler{}));
    EXPECT_TRUE(scheduler_fixture::agrees_with_schedule(borrowed_attrs_scheduler{}, environment));
    (void)attrs.query(query{}, environment);
    SUCCEED();
}

TEST(SchedulerEnvQueriesTest, LetEnvStartSchedulerIsNonthrowingForAnyConformingScheduler) {
    ex::run_loop loop;
    using selected = ex::__forge_let::__scheduler_env<throwing_copy_scheduler>;
    // MANDATE-NOTHROW of [exec.get.start.scheduler]/2 on the prvalue answer
    // of [exec.snd.expos]/10.
    static_assert(noexcept(std::declval<const selected&>().query(ex::get_start_scheduler)));
    static_assert(std::same_as<decltype(ex::get_start_scheduler(std::declval<const selected&>())),
                               throwing_copy_scheduler>);
    auto sender = ex::let_value(ex::schedule(throwing_copy_scheduler{7}), [] {
        return ex::read_env(ex::get_start_scheduler);
    });
    auto result = run_in_let_env<throwing_copy_scheduler>(std::move(sender), loop.get_scheduler());
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->resource, 7);

    auto started = ex::starts_on(throwing_copy_scheduler{9}, ex::read_env(ex::get_start_scheduler));
    auto start_result = run_in_let_env<throwing_copy_scheduler>(
        std::move(started), loop.get_scheduler());
    ASSERT_TRUE(start_result.has_value());
    EXPECT_EQ(start_result->resource, 9);
}

TEST(SchedulerEnvQueriesTest, LetValueKeepsUnaryCompletionScheduler) {
    check_unary_let_scheduler<ex::set_value_t>();
}
TEST(SchedulerEnvQueriesTest, LetErrorKeepsUnaryCompletionScheduler) {
    check_unary_let_scheduler<ex::set_error_t>();
}
TEST(SchedulerEnvQueriesTest, LetStoppedKeepsUnaryCompletionScheduler) {
    check_unary_let_scheduler<ex::set_stopped_t>();
}
TEST(SchedulerEnvQueriesTest, LetValueUsesReceiverEnvironmentForCompletionScheduler) {
    check_dependent_let_scheduler<ex::set_value_t>();
}
TEST(SchedulerEnvQueriesTest, LetErrorUsesReceiverEnvironmentForCompletionScheduler) {
    check_dependent_let_scheduler<ex::set_error_t>();
}
TEST(SchedulerEnvQueriesTest, LetStoppedUsesReceiverEnvironmentForCompletionScheduler) {
    check_dependent_let_scheduler<ex::set_stopped_t>();
}

TEST(SchedulerEnvQueriesTest, RunLoopScheduleReadsTheReceiverStopEnvironment) {
    ex::run_loop loop;
    std::inplace_stop_source source;
    source.request_stop();
    int queries = 0;
    bool stopped = false;
    auto operation = ex::connect(ex::write_env(ex::schedule(loop.get_scheduler()), forge::empty_env{}),
        stop_receiver{{source.get_token(), &queries}, &stopped});
    ex::start(operation);
    loop.finish();
    loop.run();
    EXPECT_TRUE(stopped);
    EXPECT_GT(queries, 0);
}

TEST(SchedulerEnvQueriesTest, RunLoopScheduleLetAndWriteEnvReachTheChildEnvironment) {
    ex::run_loop loop;
    auto scheduler = loop.get_scheduler();
    auto sender = ex::write_env(ex::schedule(scheduler) | ex::let_value([] {
        return ex::read_env(ex::get_scheduler);
    }), ex::prop{ex::get_scheduler, scheduler});
    // Queue before run(); finish drains queued work without requiring a worker thread.
    bool got_scheduler = false;
    struct receiver {
        using receiver_concept = ex::receiver_tag;
        ex::run_loop::scheduler expected;
        bool* result;
        void set_value(const ex::run_loop::scheduler& value) && noexcept { *result = value == expected; }
        void set_error(std::exception_ptr) && noexcept { *result = false; }
        void set_stopped() && noexcept { *result = false; }
    };
    auto operation = ex::connect(std::move(sender), receiver{scheduler, &got_scheduler});
    ex::start(operation);
    loop.finish();
    loop.run();
    EXPECT_TRUE(got_scheduler);
}

TEST(SchedulerEnvQueriesTest, CountingScopeFiltersBaseQueriesAndOverridesStopToken) {
    ex::counting_scope scope;
    std::inplace_stop_source downstream;
    auto inspect = ex::read_env([](const auto& env) noexcept {
        static_assert(!has_local<std::remove_cvref_t<decltype(env)>>);
        auto actual = std::get_stop_token(env);
        static_assert(std::stoppable_token<decltype(actual)>);
        return std::pair{env.query(forwarding_key{}), actual};
    }) | ex::then([token = downstream.get_token()](auto observed) noexcept {
        auto actual = observed.second;
        if constexpr (std::same_as<decltype(actual), decltype(token)>) {
            return observed.first == 10 && actual != token;
        } else {
            static_assert(std::same_as<decltype(actual),
                ex::__forge_counting_scope::__fused_stop_token<std::inplace_stop_token>>);
            return observed.first == 10 && actual.stop_possible() && !actual.stop_requested();
        }
    });
    auto base = ex::env{probe_env{10, 20}, ex::prop(std::get_stop_token, downstream.get_token())};
    auto result = forge::sync_wait(ex::write_env(
        scope.get_token().wrap(std::move(inspect)), base));
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(std::get<0>(*result));
    (void)forge::sync_wait(scope.join());
}

TEST(SchedulerEnvQueriesTest, SpawnFutureFiltersOverlayQueriesAndOverridesStopToken) {
    ex::simple_counting_scope scope;
    std::inplace_stop_source downstream;
    auto inspect = ex::read_env([](const auto& env) noexcept {
        static_assert(!has_local<std::remove_cvref_t<decltype(env)>>);
        auto actual = std::get_stop_token(env);
        static_assert(std::stoppable_token<decltype(actual)>);
        return std::pair{env.query(forwarding_key{}), actual};
    }) | ex::then([token = downstream.get_token()](auto observed) noexcept {
        auto actual = observed.second;
        if constexpr (std::same_as<decltype(actual), decltype(token)>) {
            return observed.first == 10 && actual != token;
        } else {
            static_assert(std::same_as<decltype(actual),
                ex::__forge_counting_scope::__fused_stop_token<std::inplace_stop_token>>);
            return observed.first == 10 && actual.stop_possible() && !actual.stop_requested();
        }
    });
    auto base = ex::env{probe_env{10, 20}, ex::prop(std::get_stop_token, downstream.get_token())};
    auto future = ex::spawn_future(std::move(inspect), scope.get_token(), base);
    auto result = forge::sync_wait(std::move(future));
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(std::get<0>(*result));
    (void)forge::sync_wait(scope.join());
}
