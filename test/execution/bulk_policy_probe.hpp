#pragma once

#include "test_bulk.hpp"

#include <execution>

#include <array>
#include <concepts>
#include <type_traits>
#include <utility>

namespace bulk_policy_probe {

namespace ex = std::execution;

struct policy_state {
    std::array<int, 4> constructions{};
    int alive = 0;
};

struct copyable_policy {
    policy_state* state;
    int value = 7;

    explicit copyable_policy(policy_state& s) noexcept : state(&s) { ++state->alive; }
    copyable_policy(copyable_policy& other) noexcept : state(other.state), value(other.value) {
        ++state->constructions[0];
        ++state->alive;
    }
    copyable_policy(const copyable_policy& other) noexcept : state(other.state), value(other.value) {
        ++state->constructions[1];
        ++state->alive;
    }
    copyable_policy(copyable_policy&& other) noexcept : state(other.state), value(other.value) {
        ++state->constructions[2];
        ++state->alive;
    }
    copyable_policy(const copyable_policy&& other) noexcept : state(other.state), value(other.value) {
        ++state->constructions[3];
        ++state->alive;
    }
    ~copyable_policy() { --state->alive; }
};

struct move_only_policy {
    policy_state* state;
    int value = 7;

    explicit move_only_policy(policy_state& s) noexcept : state(&s) { ++state->alive; }
    move_only_policy(const move_only_policy&) = delete;
    move_only_policy(move_only_policy&& other) noexcept : state(other.state), value(other.value) {
        ++state->constructions[2];
        ++state->alive;
    }
    ~move_only_policy() { --state->alive; }
};

struct immovable_policy {
    policy_state* state;
    int value = 7;

    explicit immovable_policy(policy_state& s) noexcept : state(&s) { ++state->alive; }
    immovable_policy(const immovable_policy&) = delete;
    immovable_policy(immovable_policy&&) = delete;
    ~immovable_policy() { --state->alive; }
};

struct explicit_copy_policy {
    int value = 7;
    explicit_copy_policy() = default;
    explicit_copy_policy(explicit_copy_policy&&) noexcept = default;
    explicit explicit_copy_policy(const explicit_copy_policy& other) noexcept : value(other.value) {}
};

struct policy_failure {};

struct throwing_copy_policy {
    bool* should_throw;

    explicit throwing_copy_policy(bool& flag) noexcept : should_throw(&flag) {}
    throwing_copy_policy(throwing_copy_policy&&) noexcept = default;
    throwing_copy_policy(const throwing_copy_policy& other) : should_throw(other.should_throw) {
        if (*should_throw) {
            throw policy_failure{};
        }
    }
};

template<bool Chunked>
struct callback;

template<>
struct callback<false> {
    int* calls = nullptr;
    void operator()(int index, int& value) const noexcept {
        if (calls) {
            ++*calls;
        }
        value += index;
    }
};

template<>
struct callback<true> {
    int* calls = nullptr;
    void operator()(int begin, int end, int& value) const noexcept {
        if (calls) {
            ++*calls;
        }
        value += end - begin;
    }
};

// Rebind only the policy capture of the serial test seam's closure, so these
// implementation-policy controls need no std trait specializations and do not
// name the outer implementation template (whose parameters may change).
template<class Bound, class Policy>
struct rebind_policy;
template<template<class, class> class Bound, class OldPolicy, class Shape, class Policy>
struct rebind_policy<Bound<OldPolicy, Shape>, Policy> { using type = Bound<Policy, Shape>; };
template<bool Chunked>
inline constexpr auto serial_bulk = [] {
    if constexpr (Chunked) return test_bulk::bulk_chunked;
    else return test_bulk::bulk_unchunked;
}();
template<bool Chunked>
using serial_bound_t = decltype(serial_bulk<Chunked>(int{}, callback<Chunked>{}));
template<bool Chunked, class Policy>
using bound_t = typename rebind_policy<serial_bound_t<Chunked>, Policy>::type;

using source_t = decltype(ex::just(10));

template<class Sender>
using stored_policy_t = decltype(std::declval<Sender>().__policy);

enum class category { lvalue, const_lvalue, rvalue, const_rvalue };

template<category Category, bool Pipe, class Bound>
auto apply(Bound& bound) {
    auto invoke = []<class C>(C&& closure) {
        if constexpr (Pipe) {
            return ex::just(10) | static_cast<C&&>(closure);
        } else {
            return static_cast<C&&>(closure)(ex::just(10));
        }
    };
    if constexpr (Category == category::lvalue) {
        return invoke(bound);
    } else if constexpr (Category == category::const_lvalue) {
        return invoke(std::as_const(bound));
    } else if constexpr (Category == category::rvalue) {
        return invoke(std::move(bound));
    } else {
        return invoke(std::move(std::as_const(bound)));
    }
}

} // namespace bulk_policy_probe
