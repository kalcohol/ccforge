#include <gtest/gtest.h>

#include <compare>
#include <cstddef>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace {

template<auto V>
struct external_constant {
    static constexpr auto value = V;
};

struct flag {
    bool value;

    friend constexpr flag operator&&(flag lhs, flag rhs) noexcept {
        return {lhs.value && rhs.value};
    }

    friend constexpr flag operator||(flag lhs, flag rhs) noexcept {
        return {lhs.value || rhs.value};
    }

    friend constexpr bool operator==(flag, flag) = default;
};

struct lookup {
    int values[3];

    constexpr const int& operator[](std::size_t index) const noexcept {
        return values[index];
    }
};

struct matrix_lookup {
    int values[2][2];

    constexpr const int& operator[](std::size_t row,
                                    std::size_t column) const noexcept {
        return values[row][column];
    }
};

struct multiplier {
    int factor;

    constexpr int operator()(int value) const noexcept {
        return factor * value;
    }
};

struct throwing_multiplier {
    int factor;

    int operator()(int value) const {
        return factor * value;
    }
};

struct strict_index {
    int value;

    friend constexpr bool operator==(strict_index, strict_index) = default;
};

struct strict_lookup {
    constexpr int operator[](strict_index index) const noexcept {
        return index.value * 2;
    }

    template<class T>
    constexpr int operator[](T) const = delete;
};

struct member_owner {
    int value;

    constexpr int add(int rhs) const noexcept {
        return value + rhs;
    }
};

union union_member_owner {
    int value;
    double other;

    constexpr int add(int rhs) const noexcept {
        return value + rhs;
    }

    // INVOKE must use .* for this union even though it is dereferenceable.
    constexpr union_member_owner& operator*() noexcept {
        ++value;
        return *this;
    }
};

struct derived_member_owner : member_owner {};
struct other_member_owner : member_owner {};

struct private_member_owner : private member_owner {
    member_owner& operator*() noexcept { return *this; }
};

struct ambiguous_member_owner : derived_member_owner, other_member_owner {
    member_owner& operator*() noexcept {
        return static_cast<derived_member_owner&>(*this);
    }
};

struct array_member_owner {
    int values[3];
};

struct overload_callable {
    constexpr int operator()(int) const noexcept {
        return 1;
    }

    template<auto X>
    constexpr int operator()(std::constant_wrapper<X, int>) const noexcept {
        return 2;
    }

    template<auto X>
    constexpr int operator()(std::constant_wrapper<X, int>, int) const noexcept {
        return 3;
    }

    constexpr int operator()(int, int) const noexcept {
        return 4;
    }
};

struct opaque {
    int value;

    friend constexpr bool operator==(opaque, opaque) = default;
};

struct runtime_static_value {
    static int value;
};

int runtime_static_value::value = 3;

class nonstructural_value {
public:
    constexpr explicit nonstructural_value(int value = 3) noexcept
        : value_(value) {}

private:
    int value_;
};

struct nonstructural_static_value {
    static constexpr nonstructural_value value{};
};

struct const_pseudo_mutators {
    int value;

    constexpr int operator++() const & { return value + 1; }
    constexpr int operator++(int) const & { return value; }
    constexpr const_pseudo_mutators operator--() const & { return {value - 1}; }
    constexpr const_pseudo_mutators operator--(int) const & { return {value}; }

    // Copying or moving the wrapped value must not select these overloads.
    constexpr int operator++() & { return -1; }
    constexpr int operator++() const && { return -2; }

#define FORGE_CW_TEST_COMPOUND(op, binary)                                      \
    constexpr int operator op(int rhs) const & { return value binary rhs; }     \
    constexpr int operator op(int) & { return -1; }

    FORGE_CW_TEST_COMPOUND(+=, +)
    FORGE_CW_TEST_COMPOUND(-=, -)
    FORGE_CW_TEST_COMPOUND(*=, *)
    FORGE_CW_TEST_COMPOUND(/=, /)
    FORGE_CW_TEST_COMPOUND(%=, %)
    FORGE_CW_TEST_COMPOUND(&=, &)
    FORGE_CW_TEST_COMPOUND(|=, |)
    FORGE_CW_TEST_COMPOUND(^=, ^)
    FORGE_CW_TEST_COMPOUND(<<=, <<)
    FORGE_CW_TEST_COMPOUND(>>=, >>)

#undef FORGE_CW_TEST_COMPOUND

    constexpr int operator=(int rhs) const & { return value * 10 + rhs; }
    constexpr int operator=(int) & { return -1; }
    constexpr int operator=(int) const && { return -2; }

    template<class T>
        requires std::is_same_v<T, const_pseudo_mutators>
    constexpr int operator=(T rhs) const & { return value * 10 + rhs.value; }
};

struct mutable_only_pseudo_mutators {
    constexpr int operator++() & { return 1; }
    constexpr int operator++(int) & { return 2; }
    constexpr int operator--() & { return 3; }
    constexpr int operator--(int) & { return 4; }
    constexpr int operator+=(int) & { return 5; }
    constexpr int operator=(int) & { return 6; }
};

struct rvalue_only_pseudo_mutators {
    constexpr int operator++() const && { return 1; }
    constexpr int operator++(int) const && { return 2; }
    constexpr int operator--() const && { return 3; }
    constexpr int operator--(int) const && { return 4; }
    constexpr int operator+=(int) const && { return 5; }
    constexpr int operator=(int) const && { return 6; }
};

template<class Result, bool Constant = true>
struct rejected_pseudo_mutators {
    static Result runtime_result() { return Result(); }

    static constexpr Result result() {
        if constexpr (Constant) {
            return Result();
        } else {
            return runtime_result();
        }
    }

    constexpr Result operator++() const & { return result(); }
    constexpr Result operator++(int) const & { return result(); }
    constexpr Result operator--() const & { return result(); }
    constexpr Result operator--(int) const & { return result(); }

#define FORGE_CW_TEST_REJECTED_COMPOUND(op)                                     \
    constexpr Result operator op(int) const & { return result(); }

    FORGE_CW_TEST_REJECTED_COMPOUND(+=)
    FORGE_CW_TEST_REJECTED_COMPOUND(-=)
    FORGE_CW_TEST_REJECTED_COMPOUND(*=)
    FORGE_CW_TEST_REJECTED_COMPOUND(/=)
    FORGE_CW_TEST_REJECTED_COMPOUND(%=)
    FORGE_CW_TEST_REJECTED_COMPOUND(&=)
    FORGE_CW_TEST_REJECTED_COMPOUND(|=)
    FORGE_CW_TEST_REJECTED_COMPOUND(^=)
    FORGE_CW_TEST_REJECTED_COMPOUND(<<=)
    FORGE_CW_TEST_REJECTED_COMPOUND(>>=)

#undef FORGE_CW_TEST_REJECTED_COMPOUND

    constexpr Result operator=(int) const & { return result(); }
};

struct runtime_only_pseudo_mutators {
    int operator++() const & { return 1; }
    int operator++(int) const & { return 2; }
    int operator--() const & { return 3; }
    int operator--(int) const & { return 4; }
    int operator+=(int) const & { return 5; }
    int operator=(int) const & { return 6; }
};

inline constexpr int pointed_value = 9;
inline constexpr member_owner member_value{11};
inline constexpr union_member_owner union_member_value{17};
inline constexpr array_member_owner array_member_value{{2, 4, 8}};

constexpr int plus_one(int value) noexcept {
    return value + 1;
}

namespace adl_probe {

struct token {
    int value;

    friend constexpr bool operator==(token, token) = default;
};

constexpr bool recognizes(token value) noexcept {
    return value.value == 17;
}

} // namespace adl_probe

template<class T>
constexpr bool adl_recognizes(T value) noexcept(noexcept(recognizes(value))) {
    return recognizes(value);
}

template<class L, class R>
concept has_comma = requires(L lhs, R rhs) {
    (lhs, rhs);
};

template<class T>
concept has_unary_plus = requires(T value) {
    +value;
};

template<class L, class R>
concept has_binary_plus = requires(L lhs, R rhs) {
    lhs + rhs;
};

template<class L, class R>
concept has_assignment = requires {
    std::declval<L>() = std::declval<R>();
};

template<class L, class R>
concept has_plus_assignment = requires {
    std::declval<L>() += std::declval<R>();
};

template<class L, class R>
inline constexpr bool rejects_pseudo_mutators =
    !requires { ++std::declval<L>(); } &&
    !requires { std::declval<L>()++; } &&
    !requires { --std::declval<L>(); } &&
    !requires { std::declval<L>()--; } &&
    !has_assignment<L, R> &&
    !has_plus_assignment<L, R> &&
    !requires { std::declval<L>() -= std::declval<R>(); } &&
    !requires { std::declval<L>() *= std::declval<R>(); } &&
    !requires { std::declval<L>() /= std::declval<R>(); } &&
    !requires { std::declval<L>() %= std::declval<R>(); } &&
    !requires { std::declval<L>() &= std::declval<R>(); } &&
    !requires { std::declval<L>() |= std::declval<R>(); } &&
    !requires { std::declval<L>() ^= std::declval<R>(); } &&
    !requires { std::declval<L>() <<= std::declval<R>(); } &&
    !requires { std::declval<L>() >>= std::declval<R>(); };

// Native implementations can advertise 202603L while Forge advertises 202606L.
static_assert(__cpp_lib_constant_wrapper >= 202603L);
static_assert(std::is_same_v<
              std::remove_cv_t<decltype(std::cw<42>)>,
              std::constant_wrapper<42>>);
static_assert(std::is_same_v<std::constant_wrapper<42>::value_type, int>);
static_assert(std::constant_wrapper<42>::value == 42);
static_assert(static_cast<int>(std::cw<42>) == 42);
static_assert(adl_recognizes(std::cw<adl_probe::token{17}>));
static_assert(!has_unary_plus<decltype(std::cw<opaque{1}>)>);
static_assert(has_comma<runtime_static_value, decltype(std::cw<1>)>);
static_assert(has_comma<nonstructural_static_value, decltype(std::cw<1>)>);
static_assert(!has_binary_plus<runtime_static_value, decltype(std::cw<1>)>);
static_assert(!has_binary_plus<
              nonstructural_static_value,
              decltype(std::cw<1>)>);

static_assert(decltype(+std::cw<-3>)::value == -3);
static_assert(decltype(-std::cw<3>)::value == -3);
static_assert(decltype(~std::cw<0u>)::value == ~0u);
static_assert(decltype(!std::cw<true>)::value == false);
static_assert(std::is_same_v<
              decltype(*std::cw<&pointed_value>),
              std::constant_wrapper<9>>);
static_assert(std::is_same_v<
              decltype(&std::cw<42>),
              std::constant_wrapper<&std::constant_wrapper<42, int>::value>>);

static_assert(decltype(std::cw<8> + std::cw<3>)::value == 11);
static_assert(decltype(std::cw<8> - std::cw<3>)::value == 5);
static_assert(decltype(std::cw<8> * std::cw<3>)::value == 24);
static_assert(decltype(std::cw<8> / std::cw<3>)::value == 2);
static_assert(decltype(std::cw<8> % std::cw<3>)::value == 2);
static_assert(decltype(std::cw<1u> << std::cw<3u>)::value == 8u);
static_assert(decltype(std::cw<8u> >> std::cw<2u>)::value == 2u);
static_assert(decltype(std::cw<6u> & std::cw<3u>)::value == 2u);
static_assert(decltype(std::cw<6u> | std::cw<3u>)::value == 7u);
static_assert(decltype(std::cw<6u> ^ std::cw<3u>)::value == 5u);

static_assert(decltype(std::cw<2> < std::cw<3>)::value);
static_assert(decltype(std::cw<2> <= std::cw<2>)::value);
static_assert(decltype(std::cw<2> == std::cw<2>)::value);
static_assert(decltype(std::cw<2> != std::cw<3>)::value);
static_assert(decltype(std::cw<3> > std::cw<2>)::value);
static_assert(decltype(std::cw<3> >= std::cw<3>)::value);
static_assert(std::is_same_v<
              decltype(std::cw<2> <=> std::cw<3>),
              std::strong_ordering>);

static_assert(std::is_same_v<
              decltype(std::cw<2> + std::integral_constant<int, 3>{}),
              std::constant_wrapper<5>>);
static_assert(std::is_same_v<
              decltype(external_constant<4>{} + std::cw<3>),
              std::constant_wrapper<7>>);
static_assert(std::is_same_v<decltype(std::cw<true> && std::cw<false>), bool>);
static_assert(std::is_same_v<
              decltype(std::cw<flag{true}> && std::cw<flag{false}>),
              std::constant_wrapper<flag{false}>>);
static_assert(std::is_same_v<
              decltype(std::cw<flag{true}> || std::cw<flag{false}>),
              std::constant_wrapper<flag{true}>>);
static_assert(!has_comma<decltype(std::cw<1>), decltype(std::cw<2>)>);
static_assert(std::is_same_v<
              decltype(std::cw<&member_value>->*
                       std::cw<&member_owner::value>),
              std::constant_wrapper<11, int>>);
static_assert(std::is_same_v<
              decltype(external_constant<&member_value>{}->*
                       std::cw<&member_owner::value>),
              std::constant_wrapper<11, int>>);
static_assert(std::is_same_v<
              decltype(std::cw<&member_value>->*
                       external_constant<&member_owner::value>{}),
              std::constant_wrapper<11, int>>);

using primitive_wrapper = std::remove_cv_t<decltype(std::cw<1>)>;
static_assert(rejects_pseudo_mutators<
              decltype(std::cw<1>), decltype(std::cw<2>)>);
static_assert(rejects_pseudo_mutators<
              primitive_wrapper&, decltype(std::cw<2>)>);
static_assert(rejects_pseudo_mutators<
              decltype(std::cw<1u>), decltype(std::cw<2u>)>);
static_assert(!has_assignment<
              const primitive_wrapper&, const primitive_wrapper&>);

// Ordinary copy/move assignment remains available on non-const wrappers.
static_assert(std::is_trivially_copy_assignable_v<primitive_wrapper>);
static_assert(std::is_trivially_move_assignable_v<primitive_wrapper>);
static_assert(std::is_same_v<
              decltype(std::declval<primitive_wrapper&>() =
                       std::declval<const primitive_wrapper&>()),
              primitive_wrapper&>);
static_assert(std::is_same_v<
              decltype(std::declval<primitive_wrapper&>() = primitive_wrapper{}),
              primitive_wrapper&>);

using pseudo_wrapper =
    std::remove_cv_t<decltype(std::cw<const_pseudo_mutators{5}>)>;
static_assert(decltype(++std::cw<const_pseudo_mutators{5}>)::value == 6);
static_assert(decltype(std::cw<const_pseudo_mutators{5}>++)::value == 5);
static_assert(decltype(--std::cw<const_pseudo_mutators{5}>)::value.value == 4);
static_assert(decltype(std::cw<const_pseudo_mutators{5}>--)::value.value == 5);
static_assert(decltype(++std::declval<pseudo_wrapper&>())::value == 6);
static_assert(decltype(++std::declval<const pseudo_wrapper&>())::value == 6);
static_assert(decltype(++std::declval<pseudo_wrapper&&>())::value == 6);
static_assert(std::is_same_v<
              decltype(++std::cw<const_pseudo_mutators{5}>),
              std::remove_cv_t<decltype(std::cw<6>)>>);
static_assert(std::is_same_v<
              decltype(--std::cw<const_pseudo_mutators{5}>),
              std::remove_cv_t<decltype(std::cw<const_pseudo_mutators{4}>)>>);
static_assert(std::is_same_v<
              decltype(std::cw<const_pseudo_mutators{5}> = std::cw<8>),
              std::remove_cv_t<decltype(std::cw<58>)>>);
static_assert(std::is_same_v<
              decltype(std::cw<const_pseudo_mutators{5}> =
                       std::cw<const_pseudo_mutators{5}>),
              std::remove_cv_t<decltype(std::cw<55>)>>);
static_assert(decltype(std::cw<const_pseudo_mutators{5}> += std::cw<2>)::value == 7);
static_assert(decltype(std::cw<const_pseudo_mutators{5}> -= std::cw<2>)::value == 3);
static_assert(decltype(std::cw<const_pseudo_mutators{5}> *= std::cw<2>)::value == 10);
static_assert(decltype(std::cw<const_pseudo_mutators{5}> /= std::cw<2>)::value == 2);
static_assert(decltype(std::cw<const_pseudo_mutators{5}> %= std::cw<2>)::value == 1);
static_assert(decltype(std::cw<const_pseudo_mutators{6}> &= std::cw<3>)::value == 2);
static_assert(decltype(std::cw<const_pseudo_mutators{6}> |= std::cw<3>)::value == 7);
static_assert(decltype(std::cw<const_pseudo_mutators{6}> ^= std::cw<3>)::value == 5);
static_assert(decltype(std::cw<const_pseudo_mutators{1}> <<= std::cw<3>)::value == 8);
static_assert(decltype(std::cw<const_pseudo_mutators{8}> >>= std::cw<2>)::value == 2);
static_assert(decltype(std::cw<const_pseudo_mutators{5}> +=
                       external_constant<2>{})::value == 7);
static_assert(decltype(std::cw<const_pseudo_mutators{5}> =
                       std::integral_constant<int, 8>{})::value == 58);
static_assert(noexcept(++std::cw<const_pseudo_mutators{5}>));
static_assert(noexcept(std::cw<const_pseudo_mutators{5}>++));
static_assert(noexcept(--std::cw<const_pseudo_mutators{5}>));
static_assert(noexcept(std::cw<const_pseudo_mutators{5}>--));
static_assert(noexcept(std::cw<const_pseudo_mutators{5}> += std::cw<2>));
static_assert(noexcept(std::cw<const_pseudo_mutators{5}> = std::cw<8>));

static_assert(rejects_pseudo_mutators<
              decltype(std::cw<mutable_only_pseudo_mutators{}>),
              decltype(std::cw<2>)>);
static_assert(rejects_pseudo_mutators<
              std::remove_cv_t<decltype(std::cw<mutable_only_pseudo_mutators{}>)>&,
              decltype(std::cw<2>)>);
static_assert(rejects_pseudo_mutators<
              decltype(std::cw<rvalue_only_pseudo_mutators{}>),
              decltype(std::cw<2>)>);
static_assert(rejects_pseudo_mutators<
              decltype(std::cw<rejected_pseudo_mutators<void>{}>),
              decltype(std::cw<2>)>);
static_assert(rejects_pseudo_mutators<
              decltype(std::cw<rejected_pseudo_mutators<nonstructural_value>{}>),
              decltype(std::cw<2>)>);
static_assert(rejects_pseudo_mutators<
              decltype(std::cw<rejected_pseudo_mutators<int, false>{}>),
              decltype(std::cw<2>)>);
static_assert(rejects_pseudo_mutators<
              decltype(std::cw<runtime_only_pseudo_mutators{}>),
              decltype(std::cw<2>)>);
static_assert(!has_assignment<const pseudo_wrapper&, int>);
static_assert(!has_plus_assignment<const pseudo_wrapper&, int>);
static_assert(!has_assignment<const pseudo_wrapper&, runtime_static_value>);
static_assert(!has_plus_assignment<const pseudo_wrapper&, runtime_static_value>);
static_assert(!has_assignment<const pseudo_wrapper&, nonstructural_static_value>);
static_assert(!has_plus_assignment<
              const pseudo_wrapper&, nonstructural_static_value>);

static_assert(std::is_same_v<
              decltype(std::cw<&plus_one>(std::cw<4>)),
              std::constant_wrapper<5>>);
static_assert(std::is_same_v<
              decltype(std::cw<multiplier{3}>(std::cw<4>)),
              std::constant_wrapper<12>>);
static_assert(std::is_same_v<
              decltype(std::cw<lookup{{2, 4, 8}}>[std::cw<2zu>]),
              std::constant_wrapper<8>>);
static_assert(std::is_same_v<
              decltype(std::cw<matrix_lookup{{{1, 2}, {3, 4}}}>[
                  std::cw<1zu>, std::cw<0zu>]),
              std::constant_wrapper<3>>);
static_assert(std::is_same_v<
              decltype(std::cw<strict_lookup{}>[std::cw<strict_index{4}>]),
              std::constant_wrapper<8>>);
static_assert(std::is_same_v<
              decltype(std::cw<&member_owner::add>(
                  std::cw<member_value>, std::cw<2>)),
              std::constant_wrapper<13>>);
static_assert(std::is_same_v<
              decltype(std::cw<&member_owner::value>(std::cw<member_value>)),
              std::constant_wrapper<11>>);
static_assert(std::is_same_v<
              decltype(std::cw<&union_member_owner::value>(
                  std::cw<union_member_value>)),
              std::constant_wrapper<17>>);
static_assert(std::is_same_v<
              decltype(std::cw<&union_member_owner::value>(
                  std::cw<&union_member_value>)),
              std::constant_wrapper<17>>);
static_assert(std::cw<&union_member_owner::value>(union_member_value) == 17);
static_assert(std::cw<&union_member_owner::value>(&union_member_value) == 17);
static_assert(
    std::cw<&union_member_owner::value>(std::cref(union_member_value)) == 17);
static_assert(std::cw<&union_member_owner::add>(union_member_value, 2) == 19);

using union_member_wrapper = decltype(std::cw<&union_member_owner::value>);
static_assert(!std::is_invocable_v<union_member_wrapper, member_owner&>);
static_assert(!std::is_invocable_v<union_member_wrapper, member_owner*>);
static_assert(!std::is_invocable_v<
              union_member_wrapper, std::reference_wrapper<member_owner>>);
static_assert(!std::is_invocable_v<union_member_wrapper, union_member_owner&, int>);

using member_wrapper = decltype(std::cw<&member_owner::value>);
using member_function_wrapper = decltype(std::cw<&member_owner::add>);
static_assert(!std::is_invocable_v<member_wrapper, private_member_owner&>);
static_assert(!std::is_invocable_v<member_wrapper, ambiguous_member_owner&>);
static_assert(!std::is_invocable_v<
              member_function_wrapper, private_member_owner&, int>);
static_assert(!std::is_invocable_v<
              member_function_wrapper, ambiguous_member_owner&, int>);
#if defined(_MSC_VER)
using array_member_result_t = decltype(
    std::cw<&array_member_owner::values>(std::cw<array_member_value>));
static_assert(std::is_lvalue_reference_v<array_member_result_t>);
static_assert(std::is_array_v<std::remove_reference_t<array_member_result_t>>);
static_assert(std::extent_v<std::remove_reference_t<array_member_result_t>> == 3);
static_assert(
    std::cw<&array_member_owner::values>(std::cw<array_member_value>)[2] == 8);
#else
using array_member_result_t = decltype(
    std::cw<&array_member_owner::values>(std::cw<array_member_value>));
static_assert(!std::is_reference_v<array_member_result_t>);
static_assert(std::is_same_v<
              typename array_member_result_t::value_type,
              const int*>);
static_assert(
    array_member_result_t::value[2] == 8);
#endif
static_assert(std::is_same_v<
              decltype(std::cw<overload_callable{}>(std::cw<1>)),
              std::constant_wrapper<1>>);
static_assert(std::cw<overload_callable{}>(std::cw<1>, 2) == 3);
static_assert(noexcept(std::cw<multiplier{3}>(4)));
static_assert(!noexcept(std::cw<throwing_multiplier{3}>(4)));
static_assert(noexcept(std::cw<lookup{{2, 4, 8}}>[1zu]));
static_assert(noexcept(
    std::cw<strict_lookup{}>[std::cw<strict_index{4}>]));

TEST(ConstantWrapper, PseudoMutatorsUseConstValueWithoutMutation) {
    auto value = std::cw<const_pseudo_mutators{5}>;
    const auto constant = value;

    EXPECT_EQ((++value).value, 6);
    EXPECT_EQ((value++).value, 5);
    EXPECT_EQ((--value).value.value, 4);
    EXPECT_EQ((value--).value.value, 5);
    EXPECT_EQ((value += std::cw<2>).value, 7);
    EXPECT_EQ((constant = std::cw<8>).value, 58);
    EXPECT_EQ((constant = constant).value, 55);
    EXPECT_EQ((std::move(value) = std::cw<8>).value, 58);
    EXPECT_EQ(decltype(value)::value.value, 5);
    EXPECT_EQ(std::addressof(value = constant), std::addressof(value));
}

TEST(ConstantWrapper, RuntimeCallAndSubscriptPreserveReferencesAndNoexcept) {
    constexpr auto callable = std::cw<multiplier{3}>;
    constexpr auto values = std::cw<lookup{{2, 4, 8}}>;
    constexpr auto matrix = std::cw<matrix_lookup{{{1, 2}, {3, 4}}}>;

    EXPECT_EQ(callable(7), 21);
    EXPECT_EQ(values[1zu], 4);
    EXPECT_EQ(&values[1zu], &decltype(values)::value.values[1]);
    EXPECT_EQ((matrix[0zu, 1zu]), 2);
}

TEST(ConstantWrapper, RuntimeCallUsesInvokeMemberPointerRules) {
    member_owner value{11};
    const member_owner constant{17};
    derived_member_owner derived{{19}};
    auto add = std::cw<&member_owner::add>;
    auto member = std::cw<&member_owner::value>;

    static_assert(std::is_same_v<decltype(member(value)), int&>);
    static_assert(std::is_same_v<decltype(member(constant)), const int&>);
    static_assert(std::is_same_v<decltype(member(std::move(value))), int&&>);
    static_assert(noexcept(add(value, 2)));
    EXPECT_EQ(add(value, 2), 13);
    EXPECT_EQ(add(&value, 3), 14);
    EXPECT_EQ(add(std::ref(value), 4), 15);
    EXPECT_EQ(member(value), 11);
    EXPECT_EQ(member(&value), 11);
    EXPECT_EQ(member(std::ref(value)), 11);
    EXPECT_EQ(add(constant, 2), 19);
    EXPECT_EQ(&member(constant), &constant.value);
    EXPECT_EQ(add(derived, 2), 21);
    EXPECT_EQ(add(&derived, 3), 22);
    EXPECT_EQ(add(std::ref(derived), 4), 23);
    EXPECT_EQ(&member(derived), &derived.value);
    EXPECT_EQ(&member(&derived), &derived.value);
    EXPECT_EQ(&member(std::ref(derived)), &derived.value);
}

TEST(ConstantWrapper, RuntimeCallUsesInvokeUnionMemberPointerRules) {
    union_member_owner value{11};
    const union_member_owner constant{17};
    auto add = std::cw<&union_member_owner::add>;
    auto member = std::cw<&union_member_owner::value>;

    static_assert(std::is_same_v<decltype(member(value)), int&>);
    static_assert(std::is_same_v<decltype(member(&value)), int&>);
    static_assert(std::is_same_v<decltype(member(std::ref(value))), int&>);
    static_assert(std::is_same_v<decltype(member(constant)), const int&>);
    static_assert(std::is_same_v<decltype(member(&constant)), const int&>);
    static_assert(std::is_same_v<decltype(member(std::cref(value))), const int&>);
    static_assert(std::is_same_v<decltype(member(std::move(value))), int&&>);
    static_assert(std::is_same_v<
                  decltype(member(std::move(constant))), const int&&>);
    static_assert(noexcept(member(value)));
    static_assert(noexcept(member(&value)));
    static_assert(noexcept(member(std::ref(value))));
    static_assert(noexcept(add(value, 2)));
    EXPECT_EQ(add(value, 2), 13);
    EXPECT_EQ(add(&value, 3), 14);
    EXPECT_EQ(add(std::ref(value), 4), 15);
    EXPECT_EQ(&member(value), &value.value);
    EXPECT_EQ(&member(&value), &value.value);
    EXPECT_EQ(&member(std::ref(value)), &value.value);
    member(value) = 19;
    EXPECT_EQ(value.value, 19);
    member(&value) = 23;
    EXPECT_EQ(value.value, 23);
    member(std::ref(value)) = 29;
    EXPECT_EQ(value.value, 29);
    EXPECT_EQ(&member(constant), &constant.value);
    EXPECT_EQ(&member(&constant), &constant.value);
    EXPECT_EQ(&member(std::cref(constant)), &constant.value);
}

} // namespace
