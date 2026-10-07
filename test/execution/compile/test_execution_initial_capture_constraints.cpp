#include "../initial_capture_probe.hpp"

#include <concepts>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

using namespace initial_capture_probe;

template<kind Kind, class Fn>
consteval bool accepts() {
    using algo = std::remove_cvref_t<decltype(adaptor<Kind>)>;
    if constexpr (is_bulk<Kind>) {
        return partial_callable<Kind, Fn> && full_callable<Kind, Fn> &&
            std::is_invocable_v<algo, int, Fn> &&
            std::is_invocable_v<algo, source_t<Kind>, int, Fn>;
    } else {
        return partial_callable<Kind, Fn> && full_callable<Kind, Fn> &&
            std::is_invocable_v<algo, Fn> &&
            std::is_invocable_v<algo, source_t<Kind>, Fn>;
    }
}

template<kind Kind, class Fn>
consteval bool rejects() {
    using algo = std::remove_cvref_t<decltype(adaptor<Kind>)>;
    if constexpr (is_bulk<Kind>) {
        return !partial_callable<Kind, Fn> && !full_callable<Kind, Fn> &&
            !std::is_invocable_v<algo, int, Fn> &&
            !std::is_invocable_v<algo, source_t<Kind>, int, Fn>;
    } else {
        return !partial_callable<Kind, Fn> && !full_callable<Kind, Fn> &&
            !std::is_invocable_v<algo, Fn> &&
            !std::is_invocable_v<algo, source_t<Kind>, Fn>;
    }
}

template<kind Kind, class Fn, bool L, bool CL, bool R, bool CR>
consteval bool matrix() {
    return (L ? accepts<Kind, Fn&>() : rejects<Kind, Fn&>()) &&
        (CL ? accepts<Kind, const Fn&>() : rejects<Kind, const Fn&>()) &&
        (R ? accepts<Kind, Fn&&>() : rejects<Kind, Fn&&>()) &&
        (CR ? accepts<Kind, const Fn&&>() : rejects<Kind, const Fn&&>());
}

template<kind Kind>
consteval bool check() {
    using ordinary = callback_body<is_let<Kind>>;
    using tracked = tracked_callback<is_let<Kind>>;
    using explicit_copy = explicit_copy_callback<is_let<Kind>>;
    using move_only = move_only_callback<is_let<Kind>>;
    using no_mutable_copy = deleted_mutable_copy<is_let<Kind>>;
    using no_const_move = deleted_const_move<is_let<Kind>>;
    static_assert(std::copy_constructible<tracked>);
    static_assert(!std::is_nothrow_move_constructible_v<tracked>);
    static_assert(std::is_copy_constructible_v<explicit_copy>);
    static_assert(!std::copy_constructible<explicit_copy>);
    static_assert(matrix<Kind, ordinary, true, true, true, true>());
    static_assert(matrix<Kind, tracked, true, true, true, true>());
    static_assert(matrix<Kind, std::reference_wrapper<move_only>, true, true, true, true>());
    static_assert(rejects<Kind, volatile ordinary&>());
    static_assert(rejects<Kind, const volatile ordinary&>());
    if constexpr (is_bulk<Kind>) {
        static_assert(matrix<Kind, explicit_copy, false, false, false, false>());
        static_assert(matrix<Kind, move_only, false, false, false, false>());
        static_assert(matrix<Kind, no_mutable_copy, false, false, false, false>());
        static_assert(matrix<Kind, no_const_move, false, false, false, false>());
    } else {
        static_assert(matrix<Kind, explicit_copy, true, true, true, true>());
        static_assert(matrix<Kind, move_only, false, false, true, false>());
        static_assert(matrix<Kind, no_mutable_copy, false, true, true, true>());
        static_assert(matrix<Kind, no_const_move, true, true, true, false>());
    }
    static_assert(full_callable<Kind, ordinary, tracked_source&>);
    static_assert(full_callable<Kind, ordinary, const tracked_source&>);
    static_assert(full_callable<Kind, ordinary, tracked_source>);
    static_assert(full_callable<Kind, ordinary, const tracked_source>);
    using move_source = decltype(ex::just(std::unique_ptr<int>{}));
    static_assert(full_callable<Kind, ordinary, move_source>);
    static_assert(!full_callable<Kind, ordinary, move_source&>);
    static_assert(!full_callable<Kind, ordinary, const move_source&>);
    return true;
}

static_assert(check<kind::then>());
static_assert(check<kind::upon_error>());
static_assert(check<kind::upon_stopped>());
static_assert(check<kind::let_value>());
static_assert(check<kind::let_error>());
static_assert(check<kind::let_stopped>());
static_assert(check<kind::bulk>());
static_assert(check<kind::bulk_unchunked>());
static_assert(check<kind::bulk_chunked>());

int main() {}
