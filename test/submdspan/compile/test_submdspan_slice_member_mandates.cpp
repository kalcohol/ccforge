#include <mdspan>
#include <concepts>
#include <type_traits>
#include <utility>

#if !defined(__cpp_lib_submdspan) || __cpp_lib_submdspan < 202603L
#error This probe requires the current submdspan slice types
#endif
#if !defined(FORGE_TEST_SLICE_FIELD) || !defined(FORGE_TEST_SLICE_CASE) || !defined(FORGE_TEST_SLICE_POSITIVE)
#error Select a slice field, case, and positive/negative control explicitly
#endif
#if FORGE_TEST_SLICE_FIELD < 0 || FORGE_TEST_SLICE_FIELD > 5
#error Invalid slice field
#endif
#if FORGE_TEST_SLICE_POSITIVE != 0 && FORGE_TEST_SLICE_POSITIVE != 1
#error Invalid control selection
#endif

struct valid_constant {
    static constexpr int value = 1;
    constexpr operator int() const noexcept { return value; }
};

struct floating_constant {
    static constexpr double value = 1.0;
    constexpr operator double() const noexcept { return value; }
};

struct mutable_static_constant {
    inline static int value = 1;
    constexpr operator int() const noexcept { return 1; }
};

struct nonstatic_constant {
    int value = 1;
    constexpr operator int() const noexcept { return value; }
};

struct no_conversion_constant {
    static constexpr int value = 1;
};

struct explicit_conversion_constant {
    static constexpr int value = 1;
    explicit constexpr operator int() const noexcept { return value; }
};

struct nonconstexpr_conversion_constant {
    static constexpr int value = 1;
    operator int() const noexcept { return value; }
};

struct nonconstexpr_equality_constant {
    static constexpr int value = 1;
    constexpr operator int() const noexcept { return value; }
    friend bool operator==(nonconstexpr_equality_constant, int v) noexcept {
        return v == value;
    }
};

struct mismatched_conversion_constant {
    static constexpr int value = 1;
    constexpr operator int() const noexcept { return 2; }
    friend constexpr bool operator==(mismatched_conversion_constant, int) noexcept {
        return true;
    }
};

struct nonequality_constant {
    static constexpr int value = 1;
    constexpr operator int() const noexcept { return value; }
    friend bool operator==(nonequality_constant, nonequality_constant) = delete;
};

struct function_constant {
    static constexpr int value() noexcept { return 1; }
    constexpr operator int() const noexcept { return 1; }
};

#if FORGE_TEST_SLICE_CASE == 0
using good_type = int;
using bad_type = bool;
#elif FORGE_TEST_SLICE_CASE == 1
using good_type = int;
using bad_type = double;
#elif FORGE_TEST_SLICE_CASE == 2
using good_type = signed char;
using bad_type = char;
#elif FORGE_TEST_SLICE_CASE == 3
using good_type = int;
using bad_type = wchar_t;
#elif FORGE_TEST_SLICE_CASE == 4
using good_type = unsigned char;
using bad_type = char8_t;
#elif FORGE_TEST_SLICE_CASE == 5
using good_type = unsigned short;
using bad_type = char16_t;
#elif FORGE_TEST_SLICE_CASE == 6
using good_type = unsigned int;
using bad_type = char32_t;
#elif FORGE_TEST_SLICE_CASE == 7
using good_type = std::constant_wrapper<1>;
using bad_type = std::constant_wrapper<true>;
#elif FORGE_TEST_SLICE_CASE == 8
using good_type = std::constant_wrapper<1>;
using bad_type = std::constant_wrapper<1.5>;
#elif FORGE_TEST_SLICE_CASE == 9
using good_type = valid_constant;
using bad_type = floating_constant;
#elif FORGE_TEST_SLICE_CASE == 10
using good_type = valid_constant;
using bad_type = mutable_static_constant;
#elif FORGE_TEST_SLICE_CASE == 11
using good_type = valid_constant;
using bad_type = nonstatic_constant;
#elif FORGE_TEST_SLICE_CASE == 12
using good_type = valid_constant;
using bad_type = no_conversion_constant;
#elif FORGE_TEST_SLICE_CASE == 13
using good_type = valid_constant;
using bad_type = explicit_conversion_constant;
#elif FORGE_TEST_SLICE_CASE == 14
using good_type = valid_constant;
using bad_type = nonconstexpr_conversion_constant;
#elif FORGE_TEST_SLICE_CASE == 15
using good_type = valid_constant;
using bad_type = nonconstexpr_equality_constant;
#elif FORGE_TEST_SLICE_CASE == 16
using good_type = valid_constant;
using bad_type = mismatched_conversion_constant;
#elif FORGE_TEST_SLICE_CASE == 17
using good_type = valid_constant;
using bad_type = nonequality_constant;
#elif FORGE_TEST_SLICE_CASE == 18
using good_type = valid_constant;
using bad_type = function_constant;
#else
#error Invalid slice case
#endif

using member_type = std::conditional_t<FORGE_TEST_SLICE_POSITIVE, good_type, bad_type>;

#if FORGE_TEST_SLICE_FIELD == 0
using slice_type = std::extent_slice<member_type, int, int>;
#elif FORGE_TEST_SLICE_FIELD == 1
using slice_type = std::extent_slice<int, member_type, int>;
#elif FORGE_TEST_SLICE_FIELD == 2
using slice_type = std::extent_slice<int, int, member_type>;
#elif FORGE_TEST_SLICE_FIELD == 3
using slice_type = std::range_slice<member_type, int, int>;
#elif FORGE_TEST_SLICE_FIELD == 4
using slice_type = std::range_slice<int, member_type, int>;
#else
using slice_type = std::range_slice<int, int, member_type>;
#endif

// Force complete class instantiation, not SFINAE or an unevaluated type alias alone.
static_assert(sizeof(slice_type) > 0);
int main() { return 0; }
