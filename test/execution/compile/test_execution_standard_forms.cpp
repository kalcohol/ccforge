#include "test_bulk.hpp"
#include "sender_to.hpp"
#include <execution>

#include "../bulk_policy_probe.hpp"

#include <concepts>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace ex = std::execution;

struct custom_closure : ex::sender_adaptor_closure<custom_closure> {
    template<ex::sender S>
    auto operator()(S&& source) const -> std::decay_t<S> {
        return std::forward<S>(source);
    }
};

struct sender_with_closure_base : ex::sender_adaptor_closure<sender_with_closure_base> {
    using sender_concept = ex::sender_tag;

    template<class Self, class Env>
    static constexpr auto get_completion_signatures() noexcept
        -> ex::completion_signatures<ex::set_value_t()> { return {}; }

    template<ex::receiver R>
    auto connect(R rcvr) && -> ex::connect_result_t<decltype(ex::just()), R> {
        return ex::connect(ex::just(), std::move(rcvr));
    }
};

struct each_index {
    void operator()(int, int&) const noexcept {}
};

struct each_chunk {
    void operator()(int, int, int&) const noexcept {}
};

struct make_value {
    int operator()() const noexcept { return 42; }
};

struct tagged_domain {
    template<class Tag, class S, class E>
    constexpr S&& transform_sender(Tag, S&& source, const E&) const noexcept {
        return std::forward<S>(source);
    }
};

struct domain_receiver {
    using receiver_concept = ex::receiver_tag;

    void set_value(int) && noexcept {}
    auto get_env() const noexcept {
        return ex::env{ex::prop{ex::get_domain, tagged_domain{}}};
    }
};

struct scope_join_receiver {
    using receiver_concept = ex::receiver_tag;
    bool* completed;

    void set_value() && noexcept { *completed = true; }

    auto get_env() const noexcept {
        return ex::env{ex::prop{ex::get_start_scheduler, ex::inline_scheduler{}}};
    }
};

template<class Scope>
bool scope_closes_and_joins() {
    Scope scope;
    auto token = scope.get_token();
    auto association = token.try_associate();
    const bool acquired = static_cast<bool>(association);
    scope.close();
    const bool rejected = !token.try_associate();

    bool completed = false;
    auto op = ex::connect(scope.join(), scope_join_receiver{&completed});
    ex::start(op);
    const bool pending = !completed;
    association = {};
    return acquired && rejected && pending && completed;
}

int main() {
    auto scheduler = ex::inline_scheduler{};
    auto environment = ex::env{ex::prop(ex::get_scheduler, scheduler)};
    auto write = ex::write_env(ex::just(42), environment);
    auto unstoppable = ex::unstoppable(ex::just(42));
    static_assert(std::same_as<decltype(unstoppable),
        decltype(ex::write_env(ex::just(42), ex::prop(std::get_stop_token, std::never_stop_token{})))>);
    auto variant = ex::into_variant(ex::just(42));
    static_assert(std::same_as<decltype(variant), decltype(ex::just(42) | ex::into_variant)>);
    auto optional = ex::stopped_as_optional(ex::just(42));
    static_assert(std::same_as<decltype(optional), decltype(ex::just(42) | ex::stopped_as_optional)>);
    static_assert(std::same_as<decltype(ex::stopped_as_error(ex::just_stopped(), 42)),
        decltype(ex::just_stopped() | ex::stopped_as_error(42))>);

    static_assert(ex::sender<decltype(ex::on(scheduler, ex::just(42)))>);
    static_assert(ex::sender<decltype(ex::on(scheduler, sender_with_closure_base{}))>);
    auto source = ex::schedule(scheduler);
    auto full = ex::on(source, scheduler, ex::then(make_value{}));
    auto bound = source | ex::on(scheduler, ex::then(make_value{}));
    static_assert(std::same_as<decltype(full), decltype(bound)>);
    static_assert(ex::sender<decltype(ex::on(source, scheduler, custom_closure{}))>);
    static_assert(ex::sender<decltype(source | ex::on(scheduler, custom_closure{}))>);

    static_assert(ex::sender<decltype(test_bulk::bulk(ex::just(42), 0, each_index{}))>);
    static_assert(ex::sender<decltype(ex::just(42) | test_bulk::bulk(0, each_index{}))>);
    static_assert(ex::sender<decltype(test_bulk::bulk_chunked(ex::just(42), 0, each_chunk{}))>);
    static_assert(ex::sender<decltype(ex::just(42) | test_bulk::bulk_chunked(0, each_chunk{}))>);
    static_assert(ex::sender<decltype(test_bulk::bulk_unchunked(ex::just(42), 0, each_index{}))>);
    static_assert(ex::sender<decltype(ex::just(42) | test_bulk::bulk_unchunked(0, each_index{}))>);
#if FORGE_TEST_BULK_HAS_NATIVE_POLICIES
    static_assert(ex::sender<decltype(ex::bulk(ex::just(42), ex::seq, 0, each_index{}))>);
    static_assert(ex::sender<decltype(ex::just(42) | ex::bulk(ex::seq, 0, each_index{}))>);
    static_assert(ex::sender<decltype(ex::bulk_chunked(ex::just(42), ex::seq, 0, each_chunk{}))>);
    static_assert(ex::sender<decltype(ex::just(42) | ex::bulk_chunked(ex::seq, 0, each_chunk{}))>);
    static_assert(ex::sender<decltype(ex::bulk_unchunked(ex::just(42), ex::seq, 0, each_index{}))>);
    static_assert(ex::sender<decltype(ex::just(42) | ex::bulk_unchunked(ex::seq, 0, each_index{}))>);
#endif

    auto sndr = ex::just(42);
    auto domain_env = ex::env{ex::prop(ex::get_domain, tagged_domain{})};
    static_assert(std::same_as<decltype(domain_env), ex::env<ex::prop<ex::get_domain_t, tagged_domain>>>);
    static_assert(ex::receiver<domain_receiver> && ex::sender_in<decltype(sndr), ex::env_of_t<domain_receiver>>);
    static_assert(ex::operation_state<ex::connect_result_t<decltype(sndr), domain_receiver>>);
    static_assert(test_execution::sender_to<decltype(sndr), domain_receiver>);
    static_assert(noexcept(ex::transform_sender(sndr, domain_env)));
    auto& transformed = ex::transform_sender(sndr, domain_env);
    auto& default_transformed = ex::default_domain{}.transform_sender(ex::set_value, sndr, environment);
    static_assert(std::same_as<decltype(transformed), decltype(default_transformed)>);
    static_assert(std::same_as<ex::value_types_of_t<decltype(sndr)>, std::variant<std::tuple<int>>>);
    static_assert(std::same_as<ex::error_types_of_t<decltype(ex::just_error(42))>, std::variant<int>>);
    static_assert(ex::sends_stopped<decltype(ex::just_stopped())>);
    static_assert(!ex::sends_stopped<decltype(sndr)>);
    static_assert(ex::get_forward_progress_guarantee(ex::inline_scheduler{}) ==
                  ex::forward_progress_guarantee::weakly_parallel);

    auto read = ex::write_env(ex::read_env(ex::get_scheduler), environment);
    auto read_stop_in_successor = [] {
        return ex::let_value(ex::just(), [] { return ex::read_env(std::get_stop_token); });
    };
    auto stop_flags = ex::then([](auto token) noexcept {
        return std::pair{token.stop_possible(), token.stop_requested()};
    });
    std::inplace_stop_source stop_source;
    stop_source.request_stop();
    auto stop_env = ex::prop{std::get_stop_token, stop_source.get_token()};
    const auto with_stop = std::this_thread::sync_wait(ex::write_env(
        read_stop_in_successor() | stop_flags, stop_env));
    const auto without_stop = std::this_thread::sync_wait(ex::write_env(
        ex::unstoppable(read_stop_in_successor()) | stop_flags, stop_env));

    const bool simple_scope_ok = scope_closes_and_joins<ex::simple_counting_scope>();
    const bool counting_scope_ok = scope_closes_and_joins<ex::counting_scope>();
    const auto value = std::this_thread::sync_wait(ex::just(42));
    const auto variant_value = std::this_thread::sync_wait_with_variant(ex::just(42));

    return simple_scope_ok && counting_scope_ok &&
           value == std::optional{std::tuple{42}} &&
           variant_value && std::get<0>(std::get<0>(*variant_value)) == 42 &&
           with_stop == std::optional{std::make_tuple(std::pair{true, true})} &&
           without_stop == std::optional{std::make_tuple(std::pair{false, false})} &&
           std::this_thread::sync_wait(std::move(write)) &&
           std::this_thread::sync_wait(std::move(read)) &&
           std::this_thread::sync_wait(std::move(unstoppable)) &&
           std::this_thread::sync_wait(std::move(variant)) &&
           std::this_thread::sync_wait(std::move(optional)) &&
           std::this_thread::sync_wait(std::move(full)) &&
           std::this_thread::sync_wait(std::move(bound)) ? 0 : 1;
}
