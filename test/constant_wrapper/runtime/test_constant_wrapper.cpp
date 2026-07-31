#include <gtest/gtest.h>

#include <compare>
#include <cstddef>
#include <functional>
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

inline constexpr int pointed_value = 9;
inline constexpr member_owner member_value{11};
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

static_assert(__cpp_lib_constant_wrapper >= 202606L);
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
              std::constant_wrapper<
                  &std::constant_wrapper<42, int>::value,
                  const int*>>);

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

static_assert(decltype(++std::cw<1>)::value == 2);
static_assert(decltype(std::cw<1>++)::value == 1);
static_assert(decltype(--std::cw<2>)::value == 1);
static_assert(decltype(std::cw<2>--)::value == 2);
static_assert(std::is_same_v<
              decltype(std::cw<5> = std::cw<5>),
              std::constant_wrapper<5, int>>);
static_assert(decltype(std::cw<5> = std::cw<8>)::value == 8);
static_assert(decltype(std::cw<5> += std::cw<2>)::value == 7);
static_assert(decltype(std::cw<5> -= std::cw<2>)::value == 3);
static_assert(decltype(std::cw<5> *= std::cw<2>)::value == 10);
static_assert(decltype(std::cw<5> /= std::cw<2>)::value == 2);
static_assert(decltype(std::cw<5> %= std::cw<2>)::value == 1);
static_assert(decltype(std::cw<6u> &= std::cw<3u>)::value == 2u);
static_assert(decltype(std::cw<6u> |= std::cw<3u>)::value == 7u);
static_assert(decltype(std::cw<6u> ^= std::cw<3u>)::value == 5u);
static_assert(decltype(std::cw<1u> <<= std::cw<3u>)::value == 8u);
static_assert(decltype(std::cw<8u> >>= std::cw<2u>)::value == 2u);

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
static_assert(
    decltype(std::cw<&array_member_owner::values>(
        std::cw<array_member_value>))::value[2] == 8);
static_assert(std::is_same_v<
              decltype(std::cw<overload_callable{}>(std::cw<1>)),
              std::constant_wrapper<1>>);
static_assert(std::cw<overload_callable{}>(std::cw<1>, 2) == 3);
static_assert(noexcept(std::cw<multiplier{3}>(4)));
static_assert(!noexcept(std::cw<throwing_multiplier{3}>(4)));
static_assert(noexcept(std::cw<lookup{{2, 4, 8}}>[1zu]));
static_assert(noexcept(
    std::cw<strict_lookup{}>[std::cw<strict_index{4}>]));

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
    auto add = std::cw<&member_owner::add>;
    auto member = std::cw<&member_owner::value>;

    EXPECT_EQ(add(value, 2), 13);
    EXPECT_EQ(add(&value, 3), 14);
    EXPECT_EQ(add(std::ref(value), 4), 15);
    EXPECT_EQ(member(value), 11);
    EXPECT_EQ(member(&value), 11);
    EXPECT_EQ(member(std::ref(value)), 11);
}

} // namespace
