#include "cpp26/execution/concepts.hpp"

#include <concepts>

#if defined(FORGE_EXECUTION_TEST_STRICT_MEMBER_QUERIES) && FORGE_EXECUTION_TEST_STRICT_MEMBER_QUERIES
#error "This regression must exercise default query constraints"
#endif

namespace ex = std::execution;

namespace {
struct test_query {};
struct domain {};
struct member_domain {};

struct legacy_env {
    friend domain tag_invoke(test_query, const legacy_env&) noexcept { return {}; }
    friend domain tag_invoke(test_query, const legacy_env&, int) { return {}; }
};

struct both_env {
    auto query(test_query) const noexcept -> member_domain { return {}; }
    friend domain tag_invoke(test_query, const both_env&) { return {}; }
};

// In the base constraints, T subsumes (M || T). Adding even a constant-true
// atom to the tag branch changes that ordering and makes these calls ambiguous.
template<class Q, class E, class... Args>
    requires ex::__forge_env_detail::__queryable<Q, E, Args...>
constexpr int select_query() { return 0; }

template<class Q, class E, class... Args>
    requires ex::__forge_detail::tag_invocable<Q, E, Args...>
constexpr int select_query() { return 1; }

static_assert(select_query<test_query, const legacy_env&>() == 1);
static_assert(select_query<test_query, const legacy_env&, int>() == 1);
static_assert(select_query<test_query, const both_env&>() == 1);

static_assert(ex::__forge_env_detail::__nothrow_query<test_query, const legacy_env&>);
static_assert(!ex::__forge_env_detail::__nothrow_query<test_query, const legacy_env&, int>);
static_assert(ex::__forge_env_detail::__nothrow_query<test_query, const both_env&>);
static_assert(std::same_as<
    ex::__forge_domain::__queried_domain_t<test_query, const legacy_env&>, domain>);
static_assert(std::same_as<
    ex::__forge_domain::__queried_domain_t<test_query, const legacy_env&, int>, domain>);
static_assert(std::same_as<
    ex::__forge_domain::__queried_domain_t<test_query, const both_env&>, member_domain>);
}

int main() {}
