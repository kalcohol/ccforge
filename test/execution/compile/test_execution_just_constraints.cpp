#include <execution>
#include <array>
#include <concepts>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace ex = std::execution;

template<class... Args>
concept accepts_just = requires(Args&&... args) {
    ex::just(static_cast<Args&&>(args)...);
};

template<class... Args>
concept accepts_just_error = requires(Args&&... args) {
    ex::just_error(static_cast<Args&&>(args)...);
};

template<class T>
constexpr bool accepts_both =
    accepts_just<T> && accepts_just_error<T> &&
    std::is_invocable_v<ex::just_t, T> &&
    std::is_invocable_v<ex::just_error_t, T>;

template<class T>
constexpr bool rejects_both =
    !accepts_just<T> && !accepts_just_error<T> &&
    !std::is_invocable_v<ex::just_t, T> &&
    !std::is_invocable_v<ex::just_error_t, T>;

struct move_only {
    move_only() = default;
    move_only(move_only&&) noexcept(false) {}
    move_only(const move_only&) = delete;
    move_only& operator=(move_only&&) = delete;
};

struct mutable_copy {
    mutable_copy() = default;
    mutable_copy(mutable_copy&&) = default;
    mutable_copy(mutable_copy&) {}
    mutable_copy(const mutable_copy&) = delete;
};

struct immovable {
    immovable() = default;
    immovable(immovable&&) = delete;
    immovable(const immovable&) = delete;
};

struct explicit_copy {
    explicit_copy() = default;
    explicit_copy(explicit_copy&&) = default;
    explicit explicit_copy(const explicit_copy&) = default;
};

int sample_function(int value) noexcept { return value + 1; }
using function_t = decltype(sample_function);
using function_pointer_t = decltype(&sample_function);

static_assert(accepts_just<>);
static_assert(!accepts_just_error<>);
static_assert(!accepts_just_error<int, int>);
static_assert(accepts_just<int, double, std::unique_ptr<int>>);
static_assert(accepts_both<int&>);
static_assert(accepts_both<const int&>);
static_assert(accepts_both<volatile int&>);
static_assert(accepts_both<const char*>);
static_assert(accepts_both<std::array<int, 3>&>);
static_assert(accepts_both<const std::array<int, 3>&>);

static_assert(rejects_both<int (&)[3]>);
static_assert(rejects_both<int (&&)[3]>);
static_assert(rejects_both<const char (&)[4]>);
static_assert(rejects_both<const char (&&)[4]>);
static_assert(rejects_both<int (&)[]>);
static_assert(rejects_both<int (&)[2][3]>);
static_assert(!accepts_just<int, const char (&)[4], double>);

static_assert(std::move_constructible<move_only>);
static_assert(!std::movable<move_only>);
static_assert(!std::is_nothrow_move_constructible_v<move_only>);
static_assert(accepts_both<move_only>);
static_assert(rejects_both<move_only&>);
static_assert(rejects_both<const move_only&>);
static_assert(rejects_both<const move_only>);
static_assert(rejects_both<volatile move_only&>);
static_assert(accepts_both<mutable_copy&>);
static_assert(rejects_both<const mutable_copy&>);
static_assert(std::constructible_from<explicit_copy, const explicit_copy&>);
static_assert(!std::convertible_to<const explicit_copy&, explicit_copy>);
static_assert(accepts_both<explicit_copy&>);
static_assert(accepts_both<const explicit_copy&>);
static_assert(rejects_both<immovable>);
static_assert(accepts_both<std::reference_wrapper<immovable>>);
static_assert(accepts_both<std::reference_wrapper<function_t>>);

// movable-value excludes arrays, but permits function references to decay to pointers.
static_assert(accepts_both<function_t&>);
static_assert(accepts_both<function_pointer_t>);
static_assert(std::same_as<
    ex::completion_signatures_of_t<decltype(ex::just(sample_function))>,
    ex::completion_signatures<ex::set_value_t(function_pointer_t)>>);
static_assert(std::same_as<
    ex::completion_signatures_of_t<decltype(ex::just_error(sample_function))>,
    ex::completion_signatures<ex::set_error_t(function_pointer_t)>>);
static_assert(std::same_as<
    ex::completion_signatures_of_t<decltype(ex::just(std::declval<const int&>()))>,
    ex::completion_signatures<ex::set_value_t(int)>>);
static_assert(std::same_as<
    ex::completion_signatures_of_t<decltype(ex::just_error(std::declval<const int&>()))>,
    ex::completion_signatures<ex::set_error_t(int)>>);
static_assert(std::same_as<
    ex::completion_signatures_of_t<decltype(ex::just(std::declval<std::reference_wrapper<immovable>>()))>,
    ex::completion_signatures<ex::set_value_t(std::reference_wrapper<immovable>)>>);
static_assert(std::same_as<
    ex::completion_signatures_of_t<decltype(ex::just_error(std::declval<std::reference_wrapper<immovable>>()))>,
    ex::completion_signatures<ex::set_error_t(std::reference_wrapper<immovable>)>>);

int main() {
    move_only value;
    auto values = ex::just(std::move(value));
    auto error = ex::just_error(move_only{});
    (void)values;
    (void)error;
}
