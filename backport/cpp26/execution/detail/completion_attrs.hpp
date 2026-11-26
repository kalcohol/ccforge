// MIT License
//
// Copyright (c) 2026 CC Forge Project
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include "adaptor.hpp"

#include <memory>

// Completion attributes of the single-child adaptors ([exec.adapt.general]/3.2,
// [exec.snd.general]/3-4).
//
// A chain of these adaptors ends at an origin: the first sender below it
// that is not one of them. For an output tag the chain can produce, the
// completion domain is COMMON-DOMAIN of the origin's public answers for every
// origin completion that can deliver it, and a completion scheduler is
// reported when exactly one origin completion can. A tag that cannot be
// produced has no answer. Whether a local operation of an adaptor (its
// callable or a conversion) can throw is not examined, so no user callable is
// instantiated here: every combination of throwing and non-throwing adaptors
// is considered, and an answer is given only when all of them agree.

namespace std::execution::__forge_detail {

template<class Tag>
inline constexpr unsigned __channel_index =
    std::same_as<Tag, set_value_t> ? 0u : std::same_as<Tag, set_error_t> ? 1u : 2u;

template<class Tag>
inline constexpr unsigned __channel_bit = 1u << __channel_index<Tag>;

inline constexpr unsigned __value_bit = 1u;
inline constexpr unsigned __error_bit = 2u;
inline constexpr unsigned __stopped_bit = 4u;

// For each output channel, the input channels that become it. A certain edge
// is taken whenever its input occurs. A possible edge also includes the
// inputs whose local operation would have to throw.
struct __channel_edges {
    unsigned __certain[3];
    unsigned __possible[3];
};

inline constexpr __channel_edges __identity_edges{
    {__value_bit, __error_bit, __stopped_bit}, {__value_bit, __error_bit, __stopped_bit}};

// The value input is passed on or replaced by a value; the local operation
// can add an error at the value site (then, bulk, into_variant).
inline constexpr __channel_edges __value_map_edges{
    {__value_bit, __error_bit, __stopped_bit},
    {__value_bit, __error_bit | __value_bit, __stopped_bit}};

struct __identity_channels {
    static constexpr __channel_edges __edges = __identity_edges;
};

struct __value_map_channels {
    static constexpr __channel_edges __edges = __value_map_edges;
};

template<class Sig>
inline constexpr unsigned __signature_bit = 0u;

template<class Tag, class... Args>
inline constexpr unsigned __signature_bit<Tag(Args...)> = __channel_bit<Tag>;

template<class CS>
inline constexpr unsigned __signature_bits = 0u;

template<class... Sigs>
inline constexpr unsigned __signature_bits<completion_signatures<Sigs...>> =
    (0u | ... | __signature_bit<Sigs>);

// The feasible outcomes of one way of connecting a chain. An outcome assigns
// to each of the three output channels the set of origin channels that
// deliver it, as three 3-bit masks packed into an index. There is one outcome
// for each combination of adaptors whose local operation throws; the three
// channels of an outcome belong to the same combination.
struct __channel_outcomes {
    bool __known = false;
    bool __has[512]{};
};

constexpr unsigned __outcome_channel(unsigned outcome, unsigned index) noexcept {
    return (outcome >> (3 * index)) & 7u;
}

// A sender that is not one of these adaptors: each completion in its
// signatures delivers its own channel.
constexpr __channel_outcomes __origin_outcomes(unsigned present) noexcept {
    __channel_outcomes result{};
    result.__known = true;
    result.__has[(present & 1u) | ((present & 2u) << 3) | ((present & 4u) << 6)] = true;
    return result;
}

constexpr __channel_outcomes __map_outcomes(
    const __channel_outcomes& child, const __channel_edges& edges) noexcept {
    __channel_outcomes result{};
    if (!child.__known) return result;
    result.__known = true;
    for (unsigned outcome = 0; outcome != 512; ++outcome) {
        if (!child.__has[outcome]) continue;
        for (unsigned throwing = 0; throwing != 2; ++throwing) {
            unsigned mapped = 0;
            for (unsigned out = 0; out != 3; ++out) {
                const unsigned inputs = throwing ? edges.__possible[out] : edges.__certain[out];
                unsigned origins = 0;
                for (unsigned in = 0; in != 3; ++in) {
                    if (inputs & (1u << in)) origins |= __outcome_channel(outcome, in);
                }
                mapped |= origins << (3 * out);
            }
            result.__has[mapped] = true;
        }
    }
    return result;
}

// The sets of origin channels that deliver output channel index, over all
// feasible outcomes, as a bitmap over the eight possible sets. Bit 0 says
// that some combination cannot produce the channel.
constexpr unsigned __feasible_origin_sets(
    const __channel_outcomes& outcomes, unsigned index) noexcept {
    unsigned result = 0;
    for (unsigned outcome = 0; outcome != 512; ++outcome) {
        if (outcomes.__has[outcome]) result |= 1u << __outcome_channel(outcome, index);
    }
    return result;
}

// Only the adaptor sender that declares a node is described by it. A type
// derived from an adaptor has its own attributes and delivery.
template<class Sender>
concept __channel_node_sender =
    requires { typename Sender::__channel_node::__self_t; } &&
    std::same_as<typename Sender::__channel_node::__self_t, Sender>;

// The attributes of Sender as its adaptor stores and queries them.
template<class Sender>
using __stored_attrs_t = const std::remove_reference_t<env_of_t<Sender>>&;

// The result type of the public CPO Query on the attributes of Child, or
// void. A direct query of the child is made on get_env(child) itself, while
// the adaptor queries its stored attributes as a const lvalue, and the
// scheduler operand rules of the CPOs can tell the two apart. The direct
// form is examined first, so that nothing is asked of the stored form that
// the child does not answer directly; both must give the same result type.
template<class Query, class Child, class Env>
consteval auto __child_answer() {
    using none = std::type_identity<void>;
    if constexpr (!requires {
        Query{}(std::declval<env_of_t<Child>>(), std::declval<Env>());
    }) {
        return none{};
    } else if constexpr (!requires {
        Query{}(std::declval<__stored_attrs_t<Child>>(), std::declval<Env>());
    }) {
        return none{};
    } else {
        using direct_t = decltype(Query{}(
            std::declval<env_of_t<Child>>(), std::declval<Env>()));
        using stored_t = decltype(Query{}(
            std::declval<__stored_attrs_t<Child>>(), std::declval<Env>()));
        if constexpr (std::same_as<stored_t, direct_t>) {
            return std::type_identity<stored_t>{};
        } else {
            return none{};
        }
    }
}

// Whether the public CPO, resolving a scheduler query result of type Result
// in Envs, constructs a scheduler from an rvalue reference that some member
// returned, which would move from an object that the member only lent, or
// takes a path on which that cannot be ruled out. This follows
// __resolve_scheduler branch by branch, including the operand of each query:
// the same-type loop copies the result into an unqualified local and queries
// that local, not the incoming result.
template<class Result, class... Envs>
consteval bool __resolution_moves_from_borrowed() {
    using value_query = get_completion_scheduler_t<set_value_t>;
    if constexpr (!__forge_env_detail::__try_queryable<value_query, Result&, Envs...>) {
        return false;
    } else {
        using next_t = decltype(__forge_env_detail::__try_query<
            std::execution::get_completion_scheduler<set_value_t>>(
                std::declval<Result&>(), std::declval<Envs>()...));
        using local_t = std::remove_cvref_t<Result>;
        if constexpr (!std::same_as<local_t, std::remove_cvref_t<next_t>>) {
            return __resolution_moves_from_borrowed<next_t, Envs...>();
        } else if constexpr (std::is_lvalue_reference_v<Result> && std::same_as<Result, next_t>) {
            // The reference loop keeps pointers and constructs nothing.
            return false;
        } else if constexpr (std::is_rvalue_reference_v<Result>) {
            // The local is constructed from the borrowed result.
            return true;
        } else if constexpr (!__forge_env_detail::__try_queryable<
                                 value_query, local_t&, Envs...>) {
            return true;
        } else {
            using loop_t = decltype(__forge_env_detail::__try_query<
                std::execution::get_completion_scheduler<set_value_t>>(
                    std::declval<local_t&>(), std::declval<Envs>()...));
            return std::is_rvalue_reference_v<loop_t> ||
                !std::same_as<local_t, std::remove_cvref_t<loop_t>>;
        }
    }
}

// Types that take no part in a comparison or a COMMON-DOMAIN.
struct __no_site {};

// The domains of the origin completions that deliver one output.
template<class... Domains>
struct __site_domains {
    template<class Domain>
    using __with = std::conditional_t<std::same_as<Domain, __no_site>,
        __site_domains, __site_domains<Domains..., Domain>>;

    // void if a completion has no domain.
    static consteval auto __common() {
        if constexpr (sizeof...(Domains) == 0 || (std::same_as<Domains, void> || ...)) {
            return std::type_identity<void>{};
        } else {
            return std::type_identity<__forge_domain::__common_domain_t<Domains...>>{};
        }
    }
};

// The one type that all Ts other than __no_site are, or void: if they differ,
// if one of them is void, or if there is none.
template<class... Ts>
struct __agreed;

template<>
struct __agreed<> : std::type_identity<void> {};

template<class T, class... Ts>
struct __agreed<T, Ts...> {
    using __rest = typename __agreed<Ts...>::type;
    using type = std::conditional_t<std::same_as<T, __no_site>, __rest,
        std::conditional_t<(std::same_as<Ts, __no_site> && ...), T,
            std::conditional_t<std::same_as<T, __rest>, T, void>>>;
};

// The static description of one adaptor sender Self. The sender has a
// const-lvalue connection when its child and Data are copy constructible.
// ConstChild says whether that connection connects the child as a const
// lvalue instead of a copy. Nothing here needs a complete type until a
// completion query is made.
template<class Self, class Child, class Map, class Policy, bool ConstChild, class... Data>
struct __channel_node_t {
    using __self_t = Self;

    template<class Env>
    using __child_env = typename Policy::template __env_t<Env>;

    template<bool AsConst>
    using __child_category = std::conditional_t<AsConst && ConstChild, const Child&, Child>;

    static constexpr bool __copyable =
        (std::copy_constructible<Child> && ... && std::copy_constructible<Data>);

    // -- Decided from the edge tables alone, before anything is queried. --

    // The output channels that a chain of certain edges reaches from some
    // origin channel. Any other output either cannot be produced or exists
    // only if a local operation throws, and has no answer.
    static consteval unsigned __certain_outputs() {
        unsigned below = __value_bit | __error_bit | __stopped_bit;
        if constexpr (__channel_node_sender<Child>) {
            below = Child::__channel_node::__certain_outputs();
        }
        unsigned result = 0;
        for (unsigned out = 0; out != 3; ++out) {
            if (Map::__edges.__certain[out] & below) result |= 1u << out;
        }
        return result;
    }

    // The origin channels that the edges allow to deliver output index.
    static consteval unsigned __candidate_origins(unsigned index) {
        unsigned result = 0;
        for (unsigned in = 0; in != 3; ++in) {
            if ((Map::__edges.__possible[index] & (1u << in)) == 0) continue;
            if constexpr (__channel_node_sender<Child>) {
                result |= Child::__channel_node::__candidate_origins(in);
            } else {
                result |= 1u << in;
            }
        }
        return result;
    }

    // -- The origin. --

    // The public answer of the origin for Query in the environment that the
    // chain connects it with, or void.
    template<class Query, class Env>
    static consteval auto __origin_answer() {
        if constexpr (__channel_node_sender<Child>) {
            return Child::__channel_node::template __origin_answer<Query, __child_env<Env>>();
        } else {
            return __forge_detail::__child_answer<Query, Child, __child_env<Env>>();
        }
    }

    template<class Query, class Env>
    using __origin_answer_t = typename decltype(__origin_answer<Query, Env>())::type;

    // Whether the origin answers Query for the candidate channel Origin. A
    // channel that is not a candidate is not asked.
    template<template<class> class Query, class Origin, class Env, unsigned Candidates>
    static consteval bool __origin_answers() {
        if constexpr ((Candidates & __channel_bit<Origin>) == 0) {
            return false;
        } else {
            return !std::same_as<__origin_answer_t<Query<Origin>, Env>, void>;
        }
    }

    // Whether the origin answers Query for a channel that the edges allow to
    // deliver Tag. Asked before any signature is examined, so an adaptor
    // above a sender without completion attributes never looks at that
    // sender's signatures. Tag itself is asked first, as a forwarded query
    // would be; the other candidates only if it has no answer.
    template<template<class> class Query, class Tag, class Env>
    static consteval bool __origin_answers_candidate() {
        constexpr unsigned candidates = __candidate_origins(__channel_index<Tag>);
        if constexpr (__origin_answers<Query, Tag, Env, candidates>()) {
            return true;
        } else if constexpr (__origin_answers<Query, set_value_t, Env, candidates>()) {
            return true;
        } else if constexpr (__origin_answers<Query, set_error_t, Env, candidates>()) {
            return true;
        } else {
            return __origin_answers<Query, set_stopped_t, Env, candidates>();
        }
    }

    // -- One way of connecting the chain: a connection category and an
    //    environment form. --

    // A nested adaptor is walked through its edges, so that no signature of
    // an adaptor, and with it no user callable, is formed. Only the origin
    // is asked for its signatures.
    template<bool AsConst, class Env>
    static consteval __channel_outcomes __outcomes() {
        using category = __child_category<AsConst>;
        using child_env = __child_env<Env>;
        if constexpr (AsConst && !__copyable) {
            return {};
        } else if constexpr (__channel_node_sender<Child>) {
            return __map_outcomes(Child::__channel_node::template __outcomes<
                std::is_reference_v<category>, child_env>(), Map::__edges);
        } else if constexpr (requires { typename completion_signatures_of_t<category, child_env>; }) {
            return __map_outcomes(__origin_outcomes(__signature_bits<
                completion_signatures_of_t<category, child_env>>), Map::__edges);
        } else {
            return {};
        }
    }

    // The origin's domain for channel Origin, or __no_site when the channel
    // is not in Origins. The lookup is instantiated only when it is.
    template<class Origin, class Env, unsigned Origins>
    static consteval auto __origin_domain() {
        if constexpr ((Origins & __channel_bit<Origin>) == 0) {
            return std::type_identity<__no_site>{};
        } else {
            return std::type_identity<
                __origin_answer_t<get_completion_domain_t<Origin>, Env>>{};
        }
    }

    // COMMON-DOMAIN of the origin's domains for the channels in Origins when
    // Feasible, __no_site otherwise. void if one of them has no domain.
    template<class Env, unsigned Origins, bool Feasible>
    static consteval auto __set_domain() {
        if constexpr (!Feasible) {
            return std::type_identity<__no_site>{};
        } else {
            using value_t = __site_domains<>::__with<
                typename decltype(__origin_domain<set_value_t, Env, Origins>())::type>;
            using error_t = typename value_t::template __with<
                typename decltype(__origin_domain<set_error_t, Env, Origins>())::type>;
            using all_t = typename error_t::template __with<
                typename decltype(__origin_domain<set_stopped_t, Env, Origins>())::type>;
            return all_t::__common();
        }
    }

    // The domain for Tag in one way of connecting: __no_site if the chain
    // cannot be connected this way, void if there is no answer, and
    // otherwise the COMMON-DOMAIN on which every feasible outcome agrees. An
    // outcome in which Tag cannot be produced means no answer.
    template<class Tag, bool AsConst, class Env>
    static consteval auto __way_domain() {
        constexpr auto outcomes = __outcomes<AsConst, Env>();
        if constexpr (!outcomes.__known) {
            return std::type_identity<__no_site>{};
        } else {
            constexpr unsigned sets = __feasible_origin_sets(outcomes, __channel_index<Tag>);
            if constexpr ((sets & 1u) != 0) {
                return std::type_identity<void>{};
            } else {
                return std::type_identity<typename __agreed<
                    typename decltype(__set_domain<Env, 1u, (sets & (1u << 1)) != 0>())::type,
                    typename decltype(__set_domain<Env, 2u, (sets & (1u << 2)) != 0>())::type,
                    typename decltype(__set_domain<Env, 3u, (sets & (1u << 3)) != 0>())::type,
                    typename decltype(__set_domain<Env, 4u, (sets & (1u << 4)) != 0>())::type,
                    typename decltype(__set_domain<Env, 5u, (sets & (1u << 5)) != 0>())::type,
                    typename decltype(__set_domain<Env, 6u, (sets & (1u << 6)) != 0>())::type,
                    typename decltype(__set_domain<Env, 7u, (sets & (1u << 7)) != 0>())::type
                >::type>{};
            }
        }
    }

    // The single origin channel that delivers Tag in one way of connecting,
    // as its bit: 0 if there is no single one in every feasible outcome, 8 if
    // the chain cannot be connected this way.
    template<class Tag, bool AsConst, class Env>
    static consteval unsigned __way_origin() {
        constexpr auto outcomes = __outcomes<AsConst, Env>();
        if (!outcomes.__known) return 8u;
        constexpr unsigned sets = __feasible_origin_sets(outcomes, __channel_index<Tag>);
        return sets == (1u << 1) ? 1u : sets == (1u << 2) ? 2u : sets == (1u << 4) ? 4u : 0u;
    }

    // -- All ways of connecting. A receiver supplies its environment with any
    //    qualification and the signature CPO removes cv and reference from
    //    it; the sender is connected as an rvalue or a const lvalue. Every
    //    usable way must give the same answer. --

    template<class Env>
    static constexpr bool __one_environment_form =
        std::same_as<__child_env<Env>, __child_env<std::remove_cvref_t<Env>>>;

    template<class Tag, bool AsConst, class Env, bool Use>
    static consteval auto __way_domain_if() {
        if constexpr (!Use) {
            return std::type_identity<__no_site>{};
        } else {
            return __way_domain<Tag, AsConst, Env>();
        }
    }

    // The completion domain for Tag, or void.
    template<class Tag, class Env>
    static consteval auto __domain() {
        using none = std::type_identity<void>;
        using normalized = std::remove_cvref_t<Env>;
        if constexpr ((__certain_outputs() & __channel_bit<Tag>) == 0) {
            return none{};
        } else if constexpr (!__origin_answers_candidate<get_completion_domain_t, Tag, Env>()) {
            return none{};
        } else {
            constexpr bool both = !__one_environment_form<Env>;
            return std::type_identity<typename __agreed<
                typename decltype(__way_domain<Tag, false, Env>())::type,
                typename decltype(__way_domain<Tag, true, Env>())::type,
                typename decltype(__way_domain_if<Tag, false, normalized, both>())::type,
                typename decltype(__way_domain_if<Tag, true, normalized, both>())::type
            >::type>{};
        }
    }

    template<class Tag, class Env>
    using __domain_t = typename decltype(__domain<Tag, Env>())::type;

    // The origin channel whose scheduler is reported for Tag, as its bit, or
    // 0: in every usable way of connecting and every feasible outcome, the
    // same single origin channel must deliver Tag.
    template<class Tag, class Env>
    static consteval unsigned __sole_origin() {
        using normalized = std::remove_cvref_t<Env>;
        if constexpr ((__certain_outputs() & __channel_bit<Tag>) == 0) {
            return 0u;
        } else if constexpr (!__origin_answers_candidate<get_completion_scheduler_t, Tag, Env>()) {
            return 0u;
        } else {
            unsigned origin = 8u;
            const auto merge = [&origin](unsigned way) {
                if (way == 8u) return;
                origin = (origin == 8u || origin == way) ? way : 0u;
            };
            merge(__way_origin<Tag, false, Env>());
            merge(__way_origin<Tag, true, Env>());
            if constexpr (!__one_environment_form<Env>) {
                merge(__way_origin<Tag, false, normalized>());
                merge(__way_origin<Tag, true, normalized>());
            }
            return origin == 8u ? 0u : origin;
        }
    }

    template<class Query, class Env>
    using __child_answer_t = typename decltype(
        __forge_detail::__child_answer<Query, Child, __child_env<Env>>())::type;

    // The single origin channel behind the child's channel Site, as its bit,
    // or 0.
    template<class Site, class Env>
    static consteval unsigned __site_origin() {
        if constexpr (__channel_node_sender<Child>) {
            return Child::__channel_node::template __sole_origin<Site, __child_env<Env>>();
        } else {
            return __channel_bit<Site>;
        }
    }

    // Whether the scheduler for Tag can be taken from the child's channel
    // Site: Site certainly delivers Tag, it comes from the origin channel
    // Origin alone, and the child answers for it. The child is asked only
    // for such a channel.
    template<class Tag, class Site, class Env, unsigned Origin>
    static consteval bool __is_scheduler_site() {
        if constexpr ((Map::__edges.__certain[__channel_index<Tag>] & __channel_bit<Site>) == 0) {
            return false;
        } else if constexpr (__site_origin<Site, Env>() != Origin) {
            return false;
        } else {
            return !std::same_as<__child_answer_t<get_completion_scheduler_t<Site>, Env>, void>;
        }
    }

    // The child channel whose public scheduler answer is reported for Tag,
    // or void: one origin channel must deliver Tag in every outcome, and the
    // answer is taken from a child channel that traces to that origin
    // channel alone.
    template<class Tag, class Env>
    static consteval auto __scheduler_site() {
        using none = std::type_identity<void>;
        constexpr unsigned origin = __sole_origin<Tag, Env>();
        if constexpr (origin == 0) {
            return none{};
        } else if constexpr (__is_scheduler_site<Tag, set_value_t, Env, origin>()) {
            return std::type_identity<set_value_t>{};
        } else if constexpr (__is_scheduler_site<Tag, set_error_t, Env, origin>()) {
            return std::type_identity<set_error_t>{};
        } else if constexpr (__is_scheduler_site<Tag, set_stopped_t, Env, origin>()) {
            return std::type_identity<set_stopped_t>{};
        } else {
            return none{};
        }
    }

    template<class Tag, class Env>
    using __scheduler_site_t = typename decltype(__scheduler_site<Tag, Env>())::type;

    // Whether the child's public CPO, answering the scheduler query for Site
    // from a member of the stored attributes, would move from a scheduler
    // that a member only lends.
    template<class Site, class Env>
    static consteval bool __asking_moves_from_borrowed() {
        using site_query = get_completion_scheduler_t<Site>;
        using stored_t = __stored_attrs_t<Child>;
        using child_env = __child_env<Env>;
        if constexpr (!__forge_env_detail::__try_queryable<site_query, stored_t, child_env>) {
            return false;
        } else {
            return __resolution_moves_from_borrowed<
                decltype(__forge_env_detail::__try_query<
                    std::execution::get_completion_scheduler<Site>>(
                        std::declval<stored_t>(), std::declval<child_env>())),
                child_env>();
        }
    }

    // The public CPO resolves a published scheduler again, in the supplied
    // environment. The answer is published only when that resolution returns
    // the same type and resource and moves from nothing borrowed.
    //
    // A scheduler without any value query is returned as it is. Otherwise the
    // resolver copies it into an unqualified local and asks that local's
    // value query until the answer repeats. That stops at once with the
    // published scheduler only if the child's CPO already ran the same loop:
    // the result came from a member (not from the scheduler operand itself,
    // which is returned unresolved), it is unqualified, no value query of it
    // can see either environment, and its unary value query yields a value
    // of the same type.
    template<class Published, class Env, bool FromMember>
    static consteval bool __stable_resolution() {
        using value_query = get_completion_scheduler_t<set_value_t>;
        if constexpr (!__forge_env_detail::__try_queryable<value_query, Published&, Env>) {
            return true;
        } else if constexpr (!FromMember || !std::same_as<Published, std::remove_cv_t<Published>> ||
                             __forge_env_detail::__queryable<value_query, Published&, Env> ||
                             __forge_env_detail::__queryable<
                                 value_query, Published&, __child_env<Env>>) {
            return false;
        } else {
            using next_t = decltype(__forge_env_detail::__try_query<
                std::execution::get_completion_scheduler<set_value_t>>(
                    std::declval<Published&>()));
            return !std::is_rvalue_reference_v<next_t> &&
                std::same_as<std::remove_cvref_t<next_t>, Published>;
        }
    }

    // The type with which the scheduler for Tag is published, or void.
    template<class Tag, class Env>
    static consteval auto __scheduler() {
        using none = std::type_identity<void>;
        using site_t = __scheduler_site_t<Tag, Env>;
        if constexpr (std::same_as<site_t, void>) {
            return none{};
        } else {
            using site_query = get_completion_scheduler_t<site_t>;
            using result_t = __child_answer_t<site_query, Env>;
            constexpr bool from_member = __forge_env_detail::__try_queryable<
                site_query, __stored_attrs_t<Child>, __child_env<Env>>;
            if constexpr (__asking_moves_from_borrowed<site_t, Env>()) {
                // Asking the child would move from a scheduler it only lends.
                return none{};
            } else {
                // A prvalue is published as it is. A borrowed result could
                // refer to the temporary view, so it is published as a copy
                // made from a const lvalue. Queries are made on a const
                // lvalue, which makes const immaterial, but a copy must not
                // remove volatile: that changes which members answer.
                using published_t = std::conditional_t<std::is_reference_v<result_t>,
                    std::remove_cvref_t<result_t>, result_t>;
                if constexpr (std::is_reference_v<result_t> &&
                              (std::is_volatile_v<std::remove_reference_t<result_t>> ||
                               !std::is_nothrow_constructible_v<published_t, const published_t&>)) {
                    return none{};
                } else if constexpr (!Policy::template __stable_scheduler<Env>()) {
                    return none{};
                } else if constexpr (!__stable_resolution<published_t, Env, from_member>()) {
                    return none{};
                } else if constexpr (std::same_as<typename decltype(
                    __forge_domain::__completion_query_type<
                        get_completion_domain_t<set_value_t>, published_t, Env>())::type,
                    __forge_domain::__no_completion_query>) {
                    // The public domain CPO can derive nothing from this
                    // scheduler, so publishing it adds no domain answer.
                    return std::type_identity<published_t>{};
                } else if constexpr (!std::same_as<__domain_t<Tag, Env>, void>) {
                    // It could, but the domain member answers first.
                    return std::type_identity<published_t>{};
                } else {
                    return none{};
                }
            }
        }
    }

    template<class Tag, class Env>
    using __scheduler_t = typename decltype(__scheduler<Tag, Env>())::type;
};

// FWD-ENV(get_env(rcvr)): the environment of the receiver that most adaptors
// connect their child with.
struct __forwarded_child_env {
    template<class Env>
    using __env_t = __fwd_env_t<Env>;

    template<class Env>
    static consteval bool __stable_scheduler() { return true; }

    template<class Env>
    static constexpr auto __view(Env&& env) noexcept -> __fwd_env_t<Env> {
        return __fwd_env_t<Env>::__borrow(env);
    }
};

// For a receiver that returns the outer environment itself.
struct __direct_child_env {
    template<class Env>
    using __env_t = Env;

    template<class Env>
    static consteval bool __stable_scheduler() { return true; }

    template<class Env>
    static constexpr auto __view(Env&& env) noexcept -> Env&& {
        return static_cast<Env&&>(env);
    }
};

template<class Query>
inline constexpr bool __channel_query = false;

template<class Tag>
inline constexpr bool __channel_query<get_completion_scheduler_t<Tag>> = true;

// A void domain is a customization query, not a completion channel.
template<class Tag>
inline constexpr bool __channel_query<get_completion_domain_t<Tag>> = !std::same_as<Tag, void>;

// The attributes of an adaptor sender: FWD-ENV(get_env(child)) for ordinary
// queries, with the exact type of get_env(child) kept as in __fwd_attrs_view,
// and the completion queries answered from the node. There is no unary
// completion member: an answer depends on the environment, and TRY-QUERY's
// unary retry must not bring back a channel that the environment rules out.
template<class Node>
struct __completion_attrs;

template<class Self, class Child, class Map, class Policy, bool ConstChild, class... Data>
struct __completion_attrs<__channel_node_t<Self, Child, Map, Policy, ConstChild, Data...>> {
    using __child_env = env_of_t<Child>;
    using __node = __channel_node_t<Self, Child, Map, Policy, ConstChild, Data...>;

    __forge_env_detail::__attrs_operand<__child_env> __operand;
    [[no_unique_address]] Policy __policy;

    static constexpr auto __of(const Child& child, Policy policy = {}) noexcept {
        return __completion_attrs{
            __forge_env_detail::__attrs_operand<__child_env>(child), policy};
    }

    template<__forge_env_detail::__completion_tag Tag, class Env>
        requires (!std::same_as<typename __node::template __scheduler_t<Tag, Env>, void>)
    constexpr auto query(get_completion_scheduler_t<Tag>, Env&& env) const noexcept
        -> typename __node::template __scheduler_t<Tag, Env> {
        using published_t = typename __node::template __scheduler_t<Tag, Env>;
        using site_t = typename __node::template __scheduler_site_t<Tag, Env>;
        using result_t = decltype(std::execution::get_completion_scheduler<site_t>(
            static_cast<__stored_attrs_t<Child>>(__operand.__env),
            __policy.__view(static_cast<Env&&>(env))));
        if constexpr (std::is_reference_v<result_t>) {
            const published_t& borrowed = std::execution::get_completion_scheduler<site_t>(
                static_cast<__stored_attrs_t<Child>>(__operand.__env),
                __policy.__view(static_cast<Env&&>(env)));
            return published_t(borrowed);
        } else {
            return std::execution::get_completion_scheduler<site_t>(
                static_cast<__stored_attrs_t<Child>>(__operand.__env),
                __policy.__view(static_cast<Env&&>(env)));
        }
    }

    template<__forge_env_detail::__completion_tag Tag, class Env>
        requires (!std::same_as<typename __node::template __domain_t<Tag, Env>, void>)
    constexpr auto query(get_completion_domain_t<Tag>, Env&&) const noexcept
        -> typename __node::template __domain_t<Tag, Env> {
        return typename __node::template __domain_t<Tag, Env>();
    }

    template<class Query, class... Args>
        requires (!__channel_query<std::remove_cvref_t<Query>>) &&
                 __forge_env_detail::__qualified_fwd_env_query<Query> &&
                 __forge_env_detail::__raw_queryable<Query, __child_env, Args...>
    constexpr decltype(auto) query(Query&& q, Args&&... args) const
        noexcept(__forge_env_detail::__nothrow_raw_queryable<Query, __child_env, Args...>) {
        return __operand.__call(static_cast<Query&&>(q), static_cast<Args&&>(args)...);
    }
};

} // namespace std::execution::__forge_detail
