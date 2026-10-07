#include <execution>

#include <concepts>
#include <memory>
#include <type_traits>
#include <utility>

namespace ex = std::execution;
using source_t = decltype(ex::schedule(ex::inline_scheduler{}));

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

struct identity_adaptor {
    template<ex::sender S>
    auto operator()(S&& source) && {
        return static_cast<S&&>(source);
    }

    template<ex::sender S, class Self>
        requires std::derived_from<std::remove_cvref_t<Self>, identity_adaptor> &&
                 requires(Self&& self, S&& source) {
                     static_cast<Self&&>(self)(static_cast<S&&>(source));
                 }
    friend auto operator|(S&& source, Self&& self) {
        return static_cast<Self&&>(self)(static_cast<S&&>(source));
    }
};

struct copyable_adaptor : identity_adaptor {
    constexpr copyable_adaptor() = default;
    constexpr copyable_adaptor(copyable_adaptor&) noexcept {}
    constexpr copyable_adaptor(const copyable_adaptor&) noexcept {}
    constexpr copyable_adaptor(copyable_adaptor&&) noexcept {}
    constexpr copyable_adaptor(const copyable_adaptor&&) noexcept {}
};

struct move_only_adaptor : identity_adaptor {
    move_only_adaptor() = default;
    move_only_adaptor(move_only_adaptor&&) noexcept = default;
    move_only_adaptor(const move_only_adaptor&) = delete;
};

struct mutable_copy_adaptor : identity_adaptor {
    mutable_copy_adaptor() = default;
    mutable_copy_adaptor(mutable_copy_adaptor&&) noexcept = default;
    mutable_copy_adaptor(mutable_copy_adaptor&) noexcept {}
    mutable_copy_adaptor(const mutable_copy_adaptor&) = delete;
};

struct const_move_adaptor : identity_adaptor {
    const_move_adaptor() = default;
    const_move_adaptor(const_move_adaptor&&) noexcept = default;
    const_move_adaptor(const const_move_adaptor&&) noexcept {}
    const_move_adaptor(const const_move_adaptor&) = delete;
};

struct deleted_mutable_copy : identity_adaptor {
    deleted_mutable_copy() = default;
    deleted_mutable_copy(deleted_mutable_copy&&) noexcept = default;
    deleted_mutable_copy(deleted_mutable_copy&) = delete;
    deleted_mutable_copy(const deleted_mutable_copy&) noexcept = default;
};

struct deleted_const_move : identity_adaptor {
    deleted_const_move() = default;
    deleted_const_move(deleted_const_move&&) noexcept = default;
    deleted_const_move(const deleted_const_move&) noexcept = default;
    deleted_const_move(const deleted_const_move&&) = delete;
};

struct explicit_copy_adaptor : identity_adaptor {
    explicit_copy_adaptor() = default;
    explicit_copy_adaptor(explicit_copy_adaptor&&) noexcept = default;
    explicit explicit_copy_adaptor(const explicit_copy_adaptor&) noexcept {}
};

struct throwing_move_adaptor : identity_adaptor {
    throwing_move_adaptor() = default;
    throwing_move_adaptor(const throwing_move_adaptor&) noexcept = default;
    throwing_move_adaptor(throwing_move_adaptor&&) noexcept(false) {}
};

template<class C>
using bound_t = decltype(ex::on(ex::inline_scheduler{}, C{}));

static_assert(matches_cvref<bound_t<copyable_adaptor>, true, true, true, true>);
static_assert(matches_cvref<bound_t<move_only_adaptor>, false, false, true, false>);
static_assert(matches_cvref<bound_t<mutable_copy_adaptor>, true, false, true, false>);
static_assert(matches_cvref<bound_t<const_move_adaptor>, false, false, true, true>);
static_assert(matches_cvref<bound_t<deleted_mutable_copy>, false, true, true, true>);
static_assert(matches_cvref<bound_t<deleted_const_move>, true, true, true, false>);
static_assert(matches_cvref<bound_t<explicit_copy_adaptor>, true, true, true, true>);
static_assert(matches_cvref<bound_t<throwing_move_adaptor>, true, true, true, true>);

template<class C, class Scheduler = ex::inline_scheduler>
concept bindable = requires(Scheduler&& scheduler, C&& closure) {
    ex::on(static_cast<Scheduler&&>(scheduler), static_cast<C&&>(closure));
};

static_assert(bindable<move_only_adaptor>);
static_assert(!bindable<move_only_adaptor&>);
static_assert(!bindable<const move_only_adaptor&>);
static_assert(!bindable<const move_only_adaptor>);
static_assert(!bindable<copyable_adaptor, int>);

using copyable_bound = bound_t<copyable_adaptor>;
using owned_source = decltype(ex::just(std::unique_ptr<int>{}));
static_assert(accepts<copyable_bound&, source_t&>);
static_assert(accepts<copyable_bound&, const source_t&>);
static_assert(accepts<copyable_bound&, owned_source>);
static_assert(rejects<copyable_bound&, owned_source&>);
static_assert(rejects<copyable_bound&, const owned_source&>);
static_assert(rejects<copyable_bound&, int>);
static_assert(rejects<volatile copyable_bound&>);
static_assert(rejects<const volatile copyable_bound&>);
static_assert(!ex::sender<copyable_bound>);

using applied_t = decltype(std::declval<copyable_bound&>()(std::declval<source_t>()));
using full_t = decltype(ex::on(std::declval<source_t>(),
    std::declval<ex::inline_scheduler&>(), std::declval<copyable_adaptor&>()));
static_assert(std::same_as<applied_t, full_t>);
static_assert(ex::sender<decltype(ex::on(ex::inline_scheduler{}, ex::just(7)))>);

struct owned_callback {
    std::unique_ptr<int> value;
    int operator()() && noexcept { return *value; }
};

using standard_bound = decltype(ex::on(ex::inline_scheduler{},
    ex::then(owned_callback{})));
static_assert(matches_cvref<standard_bound, false, false, true, false>);

using throwing_bound = bound_t<throwing_move_adaptor>;
static_assert(std::is_nothrow_invocable_v<copyable_bound&, source_t>);
static_assert(std::is_nothrow_invocable_v<copyable_bound&&, source_t>);
static_assert(std::is_nothrow_invocable_v<throwing_bound&, source_t>);
static_assert(!std::is_nothrow_invocable_v<throwing_bound&&, source_t>);
static_assert(noexcept(std::declval<source_t>() | std::declval<copyable_bound&&>()));
static_assert(!noexcept(std::declval<source_t>() | std::declval<throwing_bound&&>()));
static_assert(!noexcept(ex::on(ex::inline_scheduler{},
    std::declval<throwing_move_adaptor>())));

struct constexpr_source {
    using sender_concept = ex::sender_t;
    auto get_env() const noexcept -> ex::empty_env { return {}; }
};

static_assert([] {
    auto bound = ex::on(ex::inline_scheduler{}, copyable_adaptor{});
    auto first = bound(constexpr_source{});
    auto second = constexpr_source{} | std::move(std::as_const(bound));
    (void)first;
    (void)second;
    return true;
}());

int main() {}
