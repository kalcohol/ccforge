#include "cpp26/execution/concepts.hpp"

#include <concepts>

namespace ex = std::execution;

namespace {
struct test_query {};
struct domain {};
struct other_domain {};

struct legacy_env {
    friend domain tag_invoke(test_query, const legacy_env&) noexcept { return {}; }
    friend domain tag_invoke(test_query, const legacy_env&, int) { return {}; }
};

struct member_env {
    domain query(const test_query&) const noexcept { return {}; }
    domain query(const test_query&, int) const { return {}; }
};

struct competing_env : member_env {
    friend other_domain tag_invoke(test_query, const competing_env&) { return {}; }
};

template<class Q, class E, class... Args>
    requires ex::__forge_env_detail::__queryable<Q, E, Args...>
constexpr int select_query() { return 0; }

template<class Q, class E, class... Args>
    requires ex::__forge_env_detail::__member_query<Q, E, Args...> &&
             ex::__forge_env_detail::__nothrow_member_query<Q, E, Args...>
constexpr int select_query() { return 1; }

template<class Q, class E, class... Args>
concept selectable = requires { select_query<Q, E, Args...>(); };

static_assert(!selectable<test_query, const legacy_env&>);
static_assert(!selectable<test_query, const legacy_env&, int>);
static_assert(select_query<test_query, const member_env&>() == 1);
static_assert(select_query<test_query, const member_env&, int>() == 0);
static_assert(select_query<test_query, const competing_env&>() == 1);

static_assert(!ex::__forge_env_detail::__nothrow_query<test_query, const legacy_env&>);
static_assert(!ex::__forge_env_detail::__nothrow_query<test_query, const legacy_env&, int>);
static_assert(ex::__forge_env_detail::__nothrow_query<test_query, const competing_env&>);
static_assert(std::same_as<
    ex::__forge_domain::__queried_domain_t<test_query, const member_env&>, domain>);
static_assert(std::same_as<
    ex::__forge_domain::__queried_domain_t<test_query, const member_env&, int>, domain>);
static_assert(std::same_as<
    ex::__forge_domain::__queried_domain_t<test_query, const competing_env&>, domain>);
}

int main() {}
