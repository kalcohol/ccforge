#include "../forwarding_attrs_probe.hpp"

#include <concepts>
#include <memory>
#include <type_traits>
#include <utility>

using namespace forwarding_attrs_probe;

template<class Query>
concept default_is_forwarding = requires {
    typename std::enable_if_t<std::forwarding_query(Query{})>;
};

template<actual_query<true> Query>
struct structural_query_witness {};
using actual_query_witness = structural_query_witness<actual_forwarding>;

template<bool Forward>
constexpr bool valid_actual_query =
    std::semiregular<actual_query<Forward>> &&
    std::is_trivially_copyable_v<actual_query<Forward>> &&
    std::is_nothrow_default_constructible_v<actual_query<Forward>> &&
    std::is_nothrow_constructible_v<actual_query<Forward>, int> &&
    std::is_nothrow_copy_constructible_v<actual_query<Forward>> &&
    std::is_nothrow_move_constructible_v<actual_query<Forward>>;

static_assert(valid_actual_query<true> && valid_actual_query<false>);
static_assert(actual_forwarding == actual_query<true>{1});
static_assert(noexcept(actual_forwarding.query(std::forwarding_query)));
static_assert(std::same_as<decltype(actual_forwarding.query(std::forwarding_query)), bool>);
static_assert(std::forwarding_query(actual_forwarding));
static_assert(!std::forwarding_query(actual_nonforwarding));
static_assert(!default_is_forwarding<actual_query<true>>);
static_assert(!default_is_forwarding<actual_query<false>>);
static_assert(std::is_invocable_v<actual_query<true>&, attributes&>);
static_assert(std::is_invocable_v<const actual_query<true>&, const attributes&>);
static_assert(std::is_invocable_v<actual_query<true>, const attributes&>);
static_assert(std::is_invocable_v<const actual_query<true>, const attributes&>);
static_assert(std::is_invocable_v<const actual_query<false>&, const attributes&>);
static_assert(ex::__forge_detail::tag_invocable<actual_query<false>, const attributes&>);
static_assert(std::same_as<std::invoke_result_t<const actual_query<true>&, const attributes&>,
    const int&>);
static_assert(std::is_nothrow_invocable_v<const actual_query<true>&, const attributes&>);
static_assert(std::is_invocable_v<const actual_query<true>&, const attributes&, argument&>);
static_assert(!std::is_nothrow_invocable_v<
    const actual_query<true>&, const attributes&, argument&>);
static_assert(default_is_forwarding<value_query>);
static_assert(default_is_forwarding<reference_query>);
static_assert(!default_is_forwarding<blocked_query>);

template<class Env, class Query, class... Args>
concept member_queryable = requires(const Env& env, Query query, Args&&... args) {
    env.query(query, static_cast<Args&&>(args)...);
};

template<class Env, class Query, class... Args>
constexpr bool queryable_both = member_queryable<Env, Query, Args...> &&
    ex::__forge_detail::tag_invocable<Query, const Env&, Args...>;

template<class Env, class Query, class... Args>
constexpr bool unavailable_both = !member_queryable<Env, Query, Args...> &&
    !ex::__forge_detail::tag_invocable<Query, const Env&, Args...>;

template<class Env>
void check_queries() {
    static_assert(unavailable_both<Env, local_query>);
    static_assert(unavailable_both<Env, blocked_query>);
    static_assert(unavailable_both<Env, missing_query>);
    // Legal actual queries outside the default-instance gate remain a documented boundary.
    static_assert(unavailable_both<Env, actual_query<true>>);
    static_assert(unavailable_both<Env, actual_query<false>>);
    static_assert(unavailable_both<Env, actual_query<true>, argument&>);
    static_assert(queryable_both<Env, value_query>);
    static_assert(queryable_both<Env, reference_query>);
    static_assert(queryable_both<Env, marked_query>);
    static_assert(!std::is_invocable_v<local_query, const Env&>);
    static_assert(!std::is_invocable_v<blocked_query, const Env&>);
    static_assert(!std::is_invocable_v<missing_query, const Env&>);
    static_assert(!std::is_invocable_v<const actual_query<true>&, const Env&>);
    static_assert(!std::is_invocable_v<const actual_query<false>&, const Env&>);

    static_assert(std::same_as<decltype(std::declval<const Env&>().query(reference_query{})),
        const int&>);
    static_assert(std::same_as<std::invoke_result_t<reference_query, const Env&>, const int&>);
    static_assert(noexcept(std::declval<const Env&>().query(value_query{})));
    static_assert(std::is_nothrow_invocable_v<reference_query, const Env&>);

    static_assert(queryable_both<Env, argument_query, argument&, int>);
    static_assert(queryable_both<Env, argument_query, argument, int>);
    static_assert(unavailable_both<Env, argument_query>);
    static_assert(unavailable_both<Env, argument_query, const argument&, int>);
    static_assert(std::is_nothrow_invocable_v<argument_query, const Env&, argument&, int>);
    static_assert(!std::is_nothrow_invocable_v<argument_query, const Env&, argument, int>);

    using domain_query = ex::get_completion_domain_t<>;
    static_assert(queryable_both<Env, domain_query, receiver_env>);
    static_assert(unavailable_both<Env, domain_query>);
    static_assert(unavailable_both<Env, domain_query, ex::empty_env>);
    static_assert(std::same_as<decltype(std::declval<const Env&>().query(
        domain_query{}, receiver_env{})), member_domain>);
    static_assert(!noexcept(std::declval<const Env&>().query(domain_query{}, receiver_env{})));
    static_assert(!ex::__forge_detail::nothrow_tag_invocable<
        domain_query, const Env&, receiver_env>);
    static_assert(std::same_as<std::invoke_result_t<domain_query, const Env&, receiver_env>,
        member_domain>);
    static_assert(std::is_nothrow_invocable_v<domain_query, const Env&, receiver_env>);
    static_assert(!std::is_invocable_v<domain_query, const Env&>);
    static_assert(!std::is_invocable_v<domain_query, const Env&, ex::empty_env>);

    using value_domain_query = ex::get_completion_domain_t<ex::set_value_t>;
    static_assert(queryable_both<Env, value_domain_query, receiver_env>);
    static_assert(unavailable_both<Env, value_domain_query>);
    static_assert(unavailable_both<Env, value_domain_query, ex::empty_env>);
    static_assert(std::same_as<decltype(std::declval<const Env&>().query(
        value_domain_query{}, receiver_env{})), tag_domain>);
    static_assert(noexcept(std::declval<const Env&>().query(value_domain_query{}, receiver_env{})));
    static_assert(ex::__forge_detail::nothrow_tag_invocable<
        value_domain_query, const Env&, receiver_env>);
    static_assert(std::same_as<std::invoke_result_t<value_domain_query, const Env&, receiver_env>,
        tag_domain>);
}

template<class S>
void check_source(S sender) {
    for_each_adaptor(sender, []<class Sender>(Sender adapted) {
        using env_t = ex::env_of_t<Sender>;
        static_assert(ex::sender<Sender>);
        if constexpr (std::is_reference_v<ex::env_of_t<S>>) {
            static_assert(std::copy_constructible<env_t>);
        } else {
            static_assert(!std::is_reference_v<env_t>);
            static_assert(std::is_move_constructible_v<env_t> ==
                std::is_move_constructible_v<ex::env_of_t<S>>);
        }
        check_queries<env_t>();
        auto attrs = ex::get_env(adapted);
        (void)attrs;
    });
}

struct constexpr_attributes {
    int value;
    constexpr int query(value_query) const noexcept { return value; }
    constexpr const int& query(actual_query<true>) const noexcept { return value; }
};

struct constexpr_source {
    using sender_concept = ex::sender_t;
    template<class Self, class Env>
    static auto get_completion_signatures() noexcept -> ex::completion_signatures<ex::set_value_t(int)> {
        return {};
    }
    constexpr auto get_env() const noexcept -> constexpr_attributes { return {21}; }
};

using constexpr_env = ex::env_of_t<decltype(ex::then(constexpr_source{}, value_callback{}))>;
constexpr constexpr_env constexpr_attrs{{21}};
static_assert(constexpr_attrs.query(value_query{}) == 21);
static_assert(value_query{}(constexpr_attrs) == 21);
static_assert(ex::__forge_detail::tag_invoke_fn(value_query{}, constexpr_attrs) == 21);
constexpr constexpr_attributes constexpr_raw_attrs{21};
static_assert(actual_forwarding(constexpr_raw_attrs) == 21);
static_assert(std::addressof(actual_forwarding(constexpr_raw_attrs)) ==
    std::addressof(constexpr_raw_attrs.value));
static_assert(!std::is_invocable_v<const actual_query<true>&, const constexpr_env&>);

int main() {
    attribute_state state;
    immovable_attributes borrowed(state);
    check_source(source<>{&state});
    check_source(source<immovable_attributes>{&state});
    check_source(source<immovable_attributes, true>{&state, &borrowed});
    check_source(source<immovable_attributes, true, false>{&state, &borrowed});
}
