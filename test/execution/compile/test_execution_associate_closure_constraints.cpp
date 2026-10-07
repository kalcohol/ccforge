#include "../associate_closure_probe.hpp"

#include <concepts>
#include <memory>
#include <type_traits>
#include <utility>

using namespace associate_closure_probe;

template<class Token>
concept bindable = requires(Token&& token) {
    ex::associate(static_cast<Token&&>(token));
};

template<class C, class S>
concept applicable = std::is_invocable_v<C, S> && requires(C&& closure, S&& sender) {
    { static_cast<C&&>(closure)(static_cast<S&&>(sender)) } -> ex::sender;
    { static_cast<S&&>(sender) | static_cast<C&&>(closure) } -> ex::sender;
};

template<class C, class S>
concept rejected = !std::is_invocable_v<C, S> &&
    !requires(C&& closure, S&& sender) { static_cast<C&&>(closure)(static_cast<S&&>(sender)); } &&
    !requires(C&& closure, S&& sender) { static_cast<S&&>(sender) | static_cast<C&&>(closure); };

template<class Token, class S>
concept full_form_applicable = requires(Token&& token, S&& sender) {
    { ex::associate(static_cast<S&&>(sender), static_cast<Token&&>(token)) } -> ex::sender;
};

static_assert(bindable<tracked_token&>);
static_assert(bindable<const tracked_token&>);
static_assert(bindable<tracked_token>);
static_assert(bindable<const tracked_token>);
static_assert(!bindable<volatile tracked_token&>);
static_assert(!bindable<int>);
static_assert(std::is_invocable_v<decltype(ex::associate), tracked_token&>);
static_assert(!std::is_invocable_v<decltype(ex::associate), volatile tracked_token&>);

using closure = decltype(ex::associate(std::declval<tracked_token>()));
using copy_source = decltype(ex::just(42));
using move_source = decltype(ex::just(std::unique_ptr<int>{}));
static_assert(std::same_as<decltype(std::declval<closure>().__token), tracked_token>);
static_assert(!ex::sender<closure>);
static_assert(noexcept(ex::associate(std::declval<tracked_token&>())));
static_assert(applicable<closure&, copy_source&>);
static_assert(applicable<const closure&, const copy_source&>);
static_assert(applicable<closure, copy_source>);
static_assert(applicable<const closure, const copy_source>);
static_assert(applicable<closure&, move_source>);
static_assert(applicable<const closure&, move_source>);
static_assert(applicable<closure, move_source>);
static_assert(applicable<const closure, move_source>);
static_assert(rejected<closure&, move_source&>);
static_assert(rejected<const closure&, const move_source&>);
static_assert(rejected<closure, move_source&>);
static_assert(rejected<const closure, const move_source>);
static_assert(rejected<closure&, int>);

static_assert(applicable<closure&, copy_source> == full_form_applicable<tracked_token&, copy_source>);
static_assert(applicable<const closure&, copy_source> == full_form_applicable<const tracked_token&, copy_source>);
static_assert(applicable<closure, copy_source> == full_form_applicable<tracked_token, copy_source>);
static_assert(applicable<const closure, copy_source> == full_form_applicable<const tracked_token, copy_source>);
static_assert(applicable<closure&, move_source&> == full_form_applicable<tracked_token&, move_source&>);
static_assert(applicable<const closure, const move_source> == full_form_applicable<const tracked_token, const move_source>);

using simple_closure = decltype(ex::associate(
    std::declval<ex::simple_counting_scope::scope_token>()));
using counting_closure = decltype(ex::associate(
    std::declval<ex::counting_scope::scope_token>()));
static_assert(applicable<simple_closure&, copy_source>);
static_assert(applicable<const counting_closure&, copy_source>);

int main() {}
