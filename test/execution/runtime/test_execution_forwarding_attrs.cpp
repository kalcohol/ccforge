#include <gtest/gtest.h>

#include "../forwarding_attrs_probe.hpp"

#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

namespace {

using namespace forwarding_attrs_probe;

struct result_state {
    int values = 0;
    int errors = 0;
    int stopped = 0;
};

struct receiver {
    using receiver_concept = ex::receiver_t;
    result_state* result;

    template<class... Args>
    void set_value(Args&&...) && noexcept { ++result->values; }
    template<class Error>
    void set_error(Error&&) && noexcept { ++result->errors; }
    void set_stopped() && noexcept { ++result->stopped; }
    auto get_env() const noexcept -> receiver_env { return {}; }
};

template<class Sender>
void check_query_values(const Sender& sender, attribute_state& state) {
    state.reads = 0;
    const int copies = state.copies;
    const int moves = state.moves;
    auto attrs = ex::get_env(sender);
    EXPECT_EQ(state.reads, 1);
    EXPECT_EQ(state.copies, copies);
    EXPECT_EQ(state.moves, moves);
    EXPECT_EQ(attrs.query(value_query{}), 17);
    EXPECT_EQ(value_query{}(attrs), 17);
    EXPECT_EQ(ex::__forge_detail::tag_invoke_fn(value_query{}, attrs), 17);
    EXPECT_EQ(attrs.query(marked_query{}), 17);
    EXPECT_EQ(marked_query{}(attrs), 17);
    EXPECT_EQ(attrs.query(reference_query{}), 17);
    EXPECT_EQ(std::addressof(attrs.query(reference_query{})),
              std::addressof(reference_query{}(attrs)));

    argument arg;
    EXPECT_EQ(attrs.query(argument_query{}, arg, 5), 25);
    EXPECT_EQ(argument_query{}(attrs, arg, 5), 25);
    EXPECT_EQ(attrs.query(argument_query{}, std::move(arg), 5), 125);
    EXPECT_EQ(argument_query{}(attrs, argument{}, 5), 125);

    auto domain = ex::get_completion_domain<>(attrs, receiver_env{});
    static_assert(std::is_same_v<decltype(domain), member_domain>);
    auto value_domain = ex::get_completion_domain<ex::set_value_t>(attrs, receiver_env{});
    static_assert(std::is_same_v<decltype(value_domain), tag_domain>);
    EXPECT_EQ(state.raw_domain_calls, 0);
}

template<class Sender>
void check_value_completion(Sender sender, attribute_state& state) {
    result_state result;
    auto op = ex::connect(std::move(sender), receiver{&result});
    ex::start(op);
    EXPECT_EQ(result.values, 1);
    EXPECT_EQ(result.errors, 0);
    EXPECT_EQ(result.stopped, 0);
    EXPECT_EQ(state.raw_domain_calls, 0);
}

} // namespace

TEST(ExecutionForwardingAttrs, OwnedValuesUseMemberFirstAndRetainLegacyTagQueries) {
    attribute_state state;
    for_each_adaptor(source<>{&state}, [&](auto sender) {
        check_query_values(sender, state);
        EXPECT_EQ(state.alive, 0);
        check_value_completion(std::move(sender), state);
        EXPECT_EQ(state.alive, 0);
    });
}

TEST(ExecutionForwardingAttrs, ImmovablePrvaluesAreOwnedWithoutCopiesOrMoves) {
    attribute_state state;
    for_each_adaptor(source<immovable_attributes>{&state}, [&](auto sender) {
        {
            auto attrs = ex::get_env(sender);
            EXPECT_EQ(state.alive, 1);
            EXPECT_EQ(value_query{}(attrs), 17);
        }
        EXPECT_EQ(state.alive, 0);
        check_query_values(sender, state);
        check_value_completion(std::move(sender), state);
        EXPECT_EQ(state.alive, 0);
    });
    EXPECT_EQ(state.copies, 0);
    EXPECT_EQ(state.moves, 0);
}

TEST(ExecutionForwardingAttrs, BorrowedNoncopyableAttributesRemainBorrowedAcrossCopies) {
    attribute_state state;
    immovable_attributes borrowed(state);
    EXPECT_EQ(actual_nonforwarding(borrowed), 17);
    auto verify = [&](auto sender) {
        check_query_values(sender, state);
        auto attrs = ex::get_env(sender);
        auto attrs_copy = attrs;
        EXPECT_EQ(std::addressof(reference_query{}(attrs)), std::addressof(borrowed.value));
        EXPECT_EQ(std::addressof(reference_query{}(attrs_copy)), std::addressof(borrowed.value));
        EXPECT_EQ(state.alive, 1);
        check_value_completion(std::move(sender), state);
    };
    for_each_adaptor(source<immovable_attributes, true>{&state, &borrowed}, verify);
    for_each_adaptor(source<immovable_attributes, true, false>{&state, &borrowed}, verify);
    EXPECT_EQ(state.alive, 1);
    EXPECT_EQ(state.copies, 0);
    EXPECT_EQ(state.moves, 0);
}

TEST(ExecutionForwardingAttrs, ActualQueriesOutsideDefaultGateRemainRawValid) {
    actual_query<true>::default_constructions = 0;
    actual_query<false>::default_constructions = 0;
    actual_query<true> runtime_query;
    EXPECT_TRUE(std::forwarding_query(runtime_query));
    EXPECT_EQ(actual_query<true>::default_constructions, 1);
    actual_query<true>::default_constructions = 0;

    attribute_state state;
    immovable_attributes borrowed(state);
    EXPECT_EQ(actual_forwarding(borrowed), 17);
    EXPECT_EQ(actual_nonforwarding(borrowed), 17);
    EXPECT_EQ(std::addressof(actual_forwarding(borrowed)), std::addressof(borrowed.value));
    argument arg;
    EXPECT_EQ(std::addressof(actual_forwarding(borrowed, arg)),
              std::addressof(borrowed.value));
    arg.value = -1;
    EXPECT_THROW((void)actual_forwarding(borrowed, arg), actual_query_failure);
    EXPECT_THROW((void)borrowed.query(actual_forwarding, arg), actual_query_failure);
    auto verify = [&](auto sender) {
        check_query_values(sender, state);
        EXPECT_EQ(actual_query<true>::default_constructions, 0);
        EXPECT_EQ(actual_query<false>::default_constructions, 0);
        auto attrs = ex::get_env(sender);
        using env_t = decltype(attrs);
        static_assert(!std::is_invocable_v<const actual_query<true>&, const env_t&>);
        static_assert(!std::is_invocable_v<const actual_query<false>&, const env_t&>);
        EXPECT_EQ(std::addressof(reference_query{}(attrs)), std::addressof(borrowed.value));
        EXPECT_EQ(state.copies, 0);
        EXPECT_EQ(state.moves, 0);
    };
    for_each_adaptor(source<immovable_attributes, true>{&state, &borrowed}, verify);
    for_each_adaptor(source<immovable_attributes, true, false>{&state, &borrowed}, verify);
}

TEST(ExecutionForwardingAttrs, RawDomainQueryPreservesThrowingMemberAndNoexceptBoundary) {
    attribute_state state;
    auto sender = ex::then(source<>{&state}, value_callback{});
    auto attrs = ex::get_env(sender);
    static_assert(!noexcept(attrs.query(ex::get_completion_domain<>, receiver_env{})));
    static_assert(noexcept(ex::get_completion_domain<>(attrs, receiver_env{})));

    EXPECT_THROW((void)attrs.query(ex::get_completion_domain<>, receiver_env{}), domain_failure);
    EXPECT_EQ(state.raw_domain_calls, 1);
    EXPECT_THROW((void)ex::__forge_detail::tag_invoke_fn(
        ex::get_completion_domain<>, attrs, receiver_env{}), domain_failure);
    EXPECT_EQ(state.raw_domain_calls, 2);
    (void)ex::get_completion_domain<>(attrs, receiver_env{});
    EXPECT_EQ(state.raw_domain_calls, 2);

    auto raw_value_domain = attrs.query(ex::get_completion_domain<ex::set_value_t>, receiver_env{});
    static_assert(std::is_same_v<decltype(raw_value_domain), tag_domain>);
    static_assert(noexcept(attrs.query(ex::get_completion_domain<ex::set_value_t>, receiver_env{})));
    EXPECT_EQ(state.raw_domain_calls, 3);
    (void)ex::get_completion_domain<ex::set_value_t>(attrs, receiver_env{});
    EXPECT_EQ(state.raw_domain_calls, 3);
}

TEST(ExecutionForwardingAttrs, NestedFiltersPreserveBorrowedReferenceAndCompletion) {
    attribute_state state;
    immovable_attributes borrowed(state);
    auto sender = ex::stopped_as_optional(ex::into_variant(ex::bulk(
        ex::then(source<immovable_attributes, true>{&state, &borrowed}, value_callback{}),
        3, bulk_callback{})));
    auto attrs = ex::get_env(sender);
    EXPECT_EQ(std::addressof(reference_query{}(attrs)), std::addressof(borrowed.value));
    EXPECT_EQ(state.reads, 1);
    EXPECT_EQ(state.copies, 0);
    EXPECT_EQ(state.moves, 0);

    auto result = ex::sync_wait(std::move(sender));
    ASSERT_TRUE(result.has_value());
    const auto& payload = std::get<0>(*result);
    ASSERT_TRUE(payload.has_value());
    EXPECT_EQ(std::get<0>(std::get<0>(*payload)), 42);
    EXPECT_EQ(state.raw_domain_calls, 0);
}

TEST(ExecutionForwardingAttrs, NestedFiltersOwnImmovablePrvalueAttributes) {
    attribute_state state;
    auto sender = ex::into_variant(ex::bulk(
        ex::then(source<immovable_attributes>{&state}, value_callback{}), 3, bulk_callback{}));
    {
        auto attrs = ex::get_env(sender);
        EXPECT_EQ(state.alive, 1);
        EXPECT_EQ(state.reads, 1);
        EXPECT_EQ(value_query{}(attrs), 17);
        EXPECT_EQ(state.copies, 0);
        EXPECT_EQ(state.moves, 0);
    }
    EXPECT_EQ(state.alive, 0);
    check_value_completion(std::move(sender), state);
    EXPECT_EQ(state.alive, 0);
}
