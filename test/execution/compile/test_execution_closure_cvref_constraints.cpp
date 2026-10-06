#include <execution>

#include <concepts>
#include <memory>
#include <type_traits>
#include <utility>

namespace ex = std::execution;
using source_t = decltype(ex::just(7));

template<class C, class S = source_t>
concept callable_closure = requires(C&& closure, S&& source) {
    { static_cast<C&&>(closure)(static_cast<S&&>(source)) } -> ex::sender;
};

template<class C, class S = source_t>
concept pipeable_closure = requires(C&& closure, S&& source) {
    { static_cast<S&&>(source) | static_cast<C&&>(closure) } -> ex::sender;
};

template<class C, class S = source_t>
constexpr bool accepts = callable_closure<C, S> && pipeable_closure<C, S> &&
    std::is_invocable_v<C, S>;

template<class C, class S = source_t>
constexpr bool rejects = !callable_closure<C, S> && !pipeable_closure<C, S> &&
    !std::is_invocable_v<C, S>;

template<class C, bool L, bool CL, bool R, bool CR>
constexpr bool matches_cvref =
    (L ? accepts<C&> : rejects<C&>) &&
    (CL ? accepts<const C&> : rejects<const C&>) &&
    (R ? accepts<C&&> : rejects<C&&>) &&
    (CR ? accepts<const C&&> : rejects<const C&&>);

template<bool ReturnsSender>
struct callback_body {
    template<class... Args>
    auto operator()(Args&&...) noexcept {
        if constexpr (ReturnsSender) {
            return ex::just(42);
        } else {
            return 42;
        }
    }
};

template<bool ReturnsSender>
struct copyable_callback : callback_body<ReturnsSender> {
    copyable_callback() = default;
    copyable_callback(copyable_callback&) noexcept {}
    copyable_callback(const copyable_callback&) noexcept {}
    copyable_callback(copyable_callback&&) noexcept {}
    copyable_callback(const copyable_callback&&) noexcept {}
};

template<bool ReturnsSender>
struct move_only_callback : callback_body<ReturnsSender> {
    move_only_callback() = default;
    move_only_callback(move_only_callback&&) noexcept = default;
    move_only_callback(const move_only_callback&) = delete;
};

template<bool ReturnsSender>
struct mutable_copy_callback : callback_body<ReturnsSender> {
    mutable_copy_callback() = default;
    mutable_copy_callback(mutable_copy_callback&&) noexcept = default;
    mutable_copy_callback(mutable_copy_callback&) noexcept {}
    mutable_copy_callback(const mutable_copy_callback&) = delete;
};

template<bool ReturnsSender>
struct const_move_callback : callback_body<ReturnsSender> {
    const_move_callback() = default;
    const_move_callback(const_move_callback&&) noexcept = default;
    const_move_callback(const const_move_callback&&) noexcept {}
    const_move_callback(const const_move_callback&) = delete;
};

template<bool ReturnsSender>
struct deleted_mutable_copy : callback_body<ReturnsSender> {
    deleted_mutable_copy() = default;
    deleted_mutable_copy(deleted_mutable_copy&&) noexcept = default;
    deleted_mutable_copy(deleted_mutable_copy&) = delete;
    deleted_mutable_copy(const deleted_mutable_copy&) noexcept = default;
};

template<bool ReturnsSender>
struct deleted_const_move : callback_body<ReturnsSender> {
    deleted_const_move() = default;
    deleted_const_move(deleted_const_move&&) noexcept = default;
    deleted_const_move(const deleted_const_move&) noexcept = default;
    deleted_const_move(const deleted_const_move&&) = delete;
};

template<bool ReturnsSender>
struct explicit_copy_callback : callback_body<ReturnsSender> {
    explicit_copy_callback() = default;
    explicit_copy_callback(explicit_copy_callback&&) noexcept = default;
    explicit explicit_copy_callback(const explicit_copy_callback&) noexcept {}
};

template<bool ReturnsSender>
struct throwing_move_callback : callback_body<ReturnsSender> {
    throwing_move_callback() = default;
    throwing_move_callback(const throwing_move_callback&) = default;
    throwing_move_callback(throwing_move_callback&&) noexcept(false) {}
};

template<template<bool> class Callback, bool L, bool CL, bool R, bool CR>
consteval bool continuation_matrix() {
    using value_fn = Callback<false>;
    using sender_fn = Callback<true>;
    return matches_cvref<decltype(ex::then(value_fn{})), L, CL, R, CR> &&
        matches_cvref<decltype(ex::upon_error(value_fn{})), L, CL, R, CR> &&
        matches_cvref<decltype(ex::upon_stopped(value_fn{})), L, CL, R, CR> &&
        matches_cvref<decltype(ex::let_value(sender_fn{})), L, CL, R, CR> &&
        matches_cvref<decltype(ex::let_error(sender_fn{})), L, CL, R, CR> &&
        matches_cvref<decltype(ex::let_stopped(sender_fn{})), L, CL, R, CR>;
}

static_assert(continuation_matrix<copyable_callback, true, true, true, true>());
static_assert(continuation_matrix<move_only_callback, false, false, true, false>());
static_assert(continuation_matrix<mutable_copy_callback, true, false, true, false>());
static_assert(continuation_matrix<const_move_callback, false, false, true, true>());
static_assert(continuation_matrix<deleted_mutable_copy, false, true, true, true>());
static_assert(continuation_matrix<deleted_const_move, true, true, true, false>());
static_assert(continuation_matrix<explicit_copy_callback, true, true, true, true>());
static_assert(continuation_matrix<throwing_move_callback, true, true, true, true>());

using bulk_fn = copyable_callback<false>;
static_assert(std::copy_constructible<bulk_fn>);
static_assert(matches_cvref<decltype(ex::bulk(3, bulk_fn{})), true, true, true, true>);
static_assert(matches_cvref<decltype(ex::bulk_unchunked(3, bulk_fn{})), true, true, true, true>);
static_assert(matches_cvref<decltype(ex::bulk_chunked(3, bulk_fn{})), true, true, true, true>);
using throwing_bulk_fn = throwing_move_callback<false>;
static_assert(std::copy_constructible<throwing_bulk_fn>);
static_assert(!std::is_nothrow_move_constructible_v<throwing_bulk_fn>);
static_assert(matches_cvref<decltype(ex::bulk(3, throwing_bulk_fn{})), true, true, true, true>);
static_assert(matches_cvref<decltype(ex::bulk_unchunked(3, throwing_bulk_fn{})), true, true, true, true>);
static_assert(matches_cvref<decltype(ex::bulk_chunked(3, throwing_bulk_fn{})), true, true, true, true>);

template<class Fn>
concept accepts_bulk_factories = requires(Fn&& fn) {
    ex::bulk(3, static_cast<Fn&&>(fn));
    ex::bulk_unchunked(3, static_cast<Fn&&>(fn));
    ex::bulk_chunked(3, static_cast<Fn&&>(fn));
};

template<class Fn>
concept rejects_bulk_factories =
    !requires(Fn&& fn) { ex::bulk(3, static_cast<Fn&&>(fn)); } &&
    !requires(Fn&& fn) { ex::bulk_unchunked(3, static_cast<Fn&&>(fn)); } &&
    !requires(Fn&& fn) { ex::bulk_chunked(3, static_cast<Fn&&>(fn)); };

static_assert(accepts_bulk_factories<bulk_fn>);
static_assert(rejects_bulk_factories<move_only_callback<false>>);
static_assert(rejects_bulk_factories<move_only_callback<false>&>);
static_assert(rejects_bulk_factories<const move_only_callback<false>&>);

using owned_source = decltype(ex::just(std::unique_ptr<int>{}));
using value_closure = decltype(ex::then(copyable_callback<false>{}));
static_assert(accepts<value_closure&, owned_source>);
static_assert(rejects<value_closure&, owned_source&>);
static_assert(rejects<value_closure&, const owned_source&>);

int main() {
    auto mutable_closure = ex::then(mutable_copy_callback<false>{});
    const auto const_closure = ex::let_value(const_move_callback<true>{});
    auto first = ex::just(7) | mutable_closure;
    auto second = std::move(const_closure)(ex::just(7));
    (void)first;
    (void)second;
}
