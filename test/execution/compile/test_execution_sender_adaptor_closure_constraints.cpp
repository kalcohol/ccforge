#include "../sender_adaptor_closure_probe.hpp"

#include <functional>

using namespace public_closure_probe;
using source = literal_sender;

namespace unrelated_pipe_probe {
// Exercise ordinary lookup as well as ADL for operands unrelated to execution.
using std::execution::operator|;

struct incomplete; // Deliberately never completed or used as a CRTP parameter.
struct operand {
    int value;
    friend constexpr int operator|(operand left, operand right) noexcept {
        return left.value | right.value;
    }
    friend constexpr int operator|(incomplete&, operand right) noexcept {
        return right.value;
    }
    friend constexpr int operator|(operand left, incomplete&) noexcept {
        return left.value;
    }
};

template<class Left, class Right>
concept has_pipe = requires(Left&& left, Right&& right) {
    static_cast<Left&&>(left) | static_cast<Right&&>(right);
};

static_assert(!has_pipe<incomplete&, incomplete&>);
static_assert(!has_pipe<incomplete&, int>);
static_assert(!has_pipe<int, incomplete&>);
static_assert(!has_pipe<source, incomplete&>);
static_assert(!has_pipe<identity, incomplete&>);
static_assert(!has_pipe<incomplete&, identity>);
static_assert(!has_pipe<source, int>);
static_assert(!has_pipe<int, identity>);
static_assert(has_pipe<int, int>);
static_assert((operand{1} | operand{2}) == 3);
static_assert(std::same_as<decltype(std::declval<incomplete&>() | operand{7}), int>);
static_assert(std::same_as<decltype(operand{7} | std::declval<incomplete&>()), int>);
static_assert(noexcept(std::declval<incomplete&>() | operand{7}));
static_assert(noexcept(operand{7} | std::declval<incomplete&>()));
} // namespace unrelated_pipe_probe

template<class D>
concept accepts_base = requires { typename ex::sender_adaptor_closure<D>; };

struct incomplete;
using incomplete_base = ex::sender_adaptor_closure<incomplete>;
static_assert(std::is_empty_v<incomplete_base>);
struct incomplete : incomplete_base {};
union union_type { int value; };
static_assert(accepts_base<identity>);
static_assert(!accepts_base<int>);
static_assert(!accepts_base<void>);
static_assert(!accepts_base<union_type>);
static_assert(!accepts_base<const identity>);
static_assert(!accepts_base<volatile identity>);
static_assert(!accepts_base<identity&>);
static_assert(!accepts_base<identity&&>);
static_assert(!accepts_base<identity*>);
static_assert(!accepts_base<identity[1]>);
static_assert(!accepts_base<int()>);

static_assert(pipeable<source&, identity&>);
static_assert(pipeable<const source&, const identity&>);
static_assert(pipeable<source, identity>);
static_assert(pipeable<source, const identity>);
static_assert(!pipeable<source, volatile identity&>);
static_assert(std::same_as<decltype(std::declval<source&>() | identity{}), source&>);
static_assert(std::same_as<decltype(std::declval<const source&>() | identity{}), const source&>);
static_assert(std::same_as<decltype(std::declval<source>() | identity{}), source&&>);
static_assert(noexcept(std::declval<source&>() | identity{}));
static_assert(noexcept(identity{} | identity{}));
static_assert(pipeable<source, rvalue_only>);
static_assert(!pipeable<source, rvalue_only&>);
static_assert(!pipeable<source, const rvalue_only&>);
static_assert(!pipeable<source, const rvalue_only>);
static_assert(pipeable<rvalue_only, identity>);
static_assert(pipeable<identity, rvalue_only>);
static_assert(!pipeable<rvalue_only&, identity>);
static_assert(!pipeable<identity, const rvalue_only>);
using move_composed = decltype(rvalue_only{} | identity{});
static_assert(pipeable<source, move_composed>);
static_assert(!pipeable<source, move_composed&>);
static_assert(!pipeable<source, const move_composed&>);
static_assert(!pipeable<source, const move_composed>);

// A permissive call operator does not relax the sender requirement on the LHS.
struct permissive : ex::sender_adaptor_closure<permissive> {
    template<class T> source operator()(T&&) const noexcept { return {1}; }
};
static_assert(!pipeable<int, permissive>);
struct callable_only {
    template<class T> source operator()(T&&) const noexcept { return {1}; }
};
static_assert(!pipeable<source, callable_only>);
static_assert(!pipeable<identity, callable_only>);
static_assert(!pipeable<callable_only, identity>);

struct wrong_d : ex::sender_adaptor_closure<identity>, callable_only {};
struct private_base : private ex::sender_adaptor_closure<private_base>, callable_only {};
struct multiple : ex::sender_adaptor_closure<multiple>, ex::sender_adaptor_closure<identity>, callable_only {};
struct private_extra : ex::sender_adaptor_closure<private_extra>, callable_only,
    private ex::sender_adaptor_closure<identity> {};
struct ambiguous;
struct path_a : ex::sender_adaptor_closure<ambiguous> {};
struct path_b : ex::sender_adaptor_closure<ambiguous> {};
struct ambiguous : path_a, path_b, callable_only {};
struct also_sender : ex::sender_adaptor_closure<also_sender>, source, callable_only {};

template<class C>
constexpr bool excluded = !pipeable<source, C> && !pipeable<identity, C> &&
    !pipeable<C, identity>;
static_assert(excluded<wrong_d>);
static_assert(excluded<private_base>);
static_assert(excluded<multiple>);
static_assert(excluded<private_extra>);
static_assert(excluded<ambiguous>);
struct inherited_library : std::remove_cvref_t<decltype(ex::into_variant)> {};
struct rederived_library : std::remove_cvref_t<decltype(ex::into_variant)>,
    ex::sender_adaptor_closure<rederived_library> {};
static_assert(excluded<inherited_library>);
static_assert(excluded<rederived_library>);

// reference_wrapper conversion does not opt its own type into closure recognition.
using library_closure = decltype(ex::into_variant());
using library_ref = decltype(std::ref(std::declval<library_closure&>()));
using library_cref = decltype(std::cref(std::declval<library_closure&>()));
static_assert(excluded<library_ref>);
static_assert(excluded<library_cref>);
static_assert(requires(library_closure& closure, source value) {
    std::ref(closure)(std::move(value));
    std::cref(closure)(std::move(value));
});

static_assert(ex::sender<also_sender>);
static_assert(!pipeable<source, also_sender>);
static_assert(!pipeable<identity, also_sender>);
static_assert(pipeable<also_sender, identity>); // Used as a sender, not a closure.

// Indirect and shared virtual inheritance are allowed when the base is unique.
struct indirect;
struct bridge : ex::sender_adaptor_closure<indirect> {};
struct indirect : bridge, callable_only {};
struct virtual_closure;
struct virtual_a : virtual ex::sender_adaptor_closure<virtual_closure> {};
struct virtual_b : virtual ex::sender_adaptor_closure<virtual_closure> {};
struct virtual_closure : virtual_a, virtual_b, callable_only {};
static_assert(pipeable<source, indirect>);
static_assert(pipeable<source, virtual_closure>);

struct volatile_closure : ex::sender_adaptor_closure<volatile_closure> {
    template<ex::sender S>
    decltype(auto) operator()(S&& value) volatile noexcept { return static_cast<S&&>(value); }
};
static_assert(pipeable<source&, volatile volatile_closure&>);

struct throwing_copy : ex::sender_adaptor_closure<throwing_copy> {
    throwing_copy() = default;
    throwing_copy(const throwing_copy&) noexcept(false) {}
    throwing_copy(throwing_copy&&) noexcept = default;
    template<ex::sender S>
    decltype(auto) operator()(S&& value) const noexcept { return static_cast<S&&>(value); }
};
static_assert(!noexcept(std::declval<throwing_copy&>() | identity{}));
static_assert(noexcept(throwing_copy{} | identity{}));
static_assert(!noexcept(std::declval<source&>() | std::declval<traced<false>&>()));
using throwing_first = decltype(std::declval<traced<false>>() | identity{});
using throwing_second = decltype(identity{} | std::declval<traced<false>>());
static_assert(!noexcept(std::declval<source&>() | std::declval<throwing_first&>()));
static_assert(!noexcept(std::declval<source&>() | std::declval<throwing_second&>()));

struct throwing_destructor : ex::sender_adaptor_closure<throwing_destructor> {
    ~throwing_destructor() noexcept(false) {}
    template<ex::sender S>
    decltype(auto) operator()(S&& value) const noexcept { return static_cast<S&&>(value); }
};
// Composition requires initialization, not the stronger constructible_from concept.
static_assert(pipeable<throwing_destructor&, identity>);

consteval bool constexpr_pipeline() {
    source value{7};
    const auto closure = identity{} | identity{};
    return &(value | closure) == &value &&
        &(value | std::move(closure)) == &value && (value | identity{}).value == 7;
}
static_assert(constexpr_pipeline());

struct extra_call : ex::sender_adaptor_closure<extra_call> {
    constexpr int operator()(int value) const noexcept { return value + 1; }
    template<ex::sender S>
    constexpr decltype(auto) operator()(S&& value) const noexcept { return static_cast<S&&>(value); }
};
constexpr auto extra_composed = extra_call{} | extra_call{};
static_assert(extra_composed(1) == 3);
static_assert(!pipeable<int, decltype(extra_composed)>);

struct literal_only : ex::sender_adaptor_closure<literal_only> {
    constexpr source operator()(source value) const noexcept { return value; }
};
// Initializing a composition does not require its call pattern to be valid yet.
using deferred_composition = decltype(scale{} | literal_only{});
static_assert(!pipeable<source, deferred_composition>);

// Every library closure opts in through the public CRTP specialization.
template<class C>
constexpr bool public_base = std::derived_from<std::remove_cvref_t<C>,
    ex::sender_adaptor_closure<std::remove_cvref_t<C>>>;
static_assert(public_base<decltype(ex::then([](int n) { return n; }))>);
static_assert(public_base<decltype(ex::upon_error([](int n) { return n; }))>);
static_assert(public_base<decltype(ex::upon_stopped([] { return 1; }))>);
static_assert(public_base<decltype(ex::let_value([](int n) { return ex::just(n); }))>);
static_assert(public_base<decltype(ex::let_error([](int n) { return ex::just(n); }))>);
static_assert(public_base<decltype(ex::let_stopped([] { return ex::just(1); }))>);
static_assert(public_base<decltype(ex::bulk(3, [](int, int&) {}))>);
static_assert(public_base<decltype(ex::bulk_unchunked(3, [](int, int&) {}))>);
static_assert(public_base<decltype(ex::bulk_chunked(3, [](int, int, int&) {}))>);
static_assert(public_base<decltype(ex::stopped_as_error(1))>);
static_assert(public_base<decltype(ex::stopped_as_optional)>);
static_assert(public_base<decltype(ex::stopped_as_optional())>);
static_assert(public_base<decltype(ex::into_variant)>);
static_assert(public_base<decltype(ex::into_variant())>);
static_assert(public_base<decltype(ex::unstoppable)>);
static_assert(public_base<decltype(ex::unstoppable())>);
static_assert(public_base<decltype(ex::affine)>);
static_assert(public_base<decltype(ex::write_env(ex::env<>{}))>);
static_assert(public_base<decltype(ex::continues_on(ex::inline_scheduler{}))>);
static_assert(public_base<decltype(ex::on(ex::inline_scheduler{}, identity{}))>);
static_assert(public_base<decltype(ex::associate(
    std::declval<ex::simple_counting_scope::scope_token>()))>);
static_assert(public_base<decltype(identity{} | identity{})>);
static_assert(pipeable<decltype(ex::just(1)), decltype(ex::unstoppable)>);

int main() {}
