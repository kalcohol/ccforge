#pragma once

#include <execution>

#include <array>
#include <concepts>
#include <type_traits>
#include <utility>

// A native <execution> header can exist without declaring policy objects.
#if defined(_LIBCPP_VERSION)
#if defined(_LIBCPP_HAS_EXPERIMENTAL_PSTL)
#if _LIBCPP_HAS_EXPERIMENTAL_PSTL && _LIBCPP_STD_VER >= 17
#define FORGE_TEST_BULK_HAS_NATIVE_POLICIES 1
#endif
#elif _LIBCPP_VERSION < 200000 && !defined(_LIBCPP_HAS_NO_INCOMPLETE_PSTL) && _LIBCPP_STD_VER >= 17
#define FORGE_TEST_BULK_HAS_NATIVE_POLICIES 1
#endif
#elif defined(_PSTL_EXECUTION_POLICIES_DEFINED) || \
    (defined(__cpp_lib_execution) && __cpp_lib_execution >= 201603L)
#define FORGE_TEST_BULK_HAS_NATIVE_POLICIES 1
#endif

#ifndef FORGE_TEST_BULK_HAS_NATIVE_POLICIES
#define FORGE_TEST_BULK_HAS_NATIVE_POLICIES 0
#endif

#if FORGE_TEST_BULK_HAS_NATIVE_POLICIES && \
    ((defined(_LIBCPP_VERSION) && _LIBCPP_STD_VER >= 20) || \
     (defined(__cpp_lib_execution) && __cpp_lib_execution >= 201902L))
#define FORGE_TEST_BULK_HAS_UNSEQ 1
#else
#define FORGE_TEST_BULK_HAS_UNSEQ 0
#endif

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

// Implementation probes, not user specializations of is_execution_policy.
template<bool Chunked, class Policy>
using bound_t = typename ex::__forge_bulk::__bulk_closure<Chunked, callback<Chunked>>
    ::template __with_shape<Policy, int>;

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
