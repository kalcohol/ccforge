#include <utility>

#include <type_traits>

struct array_member_owner {
    int values[3];
};

inline constexpr array_member_owner array_member_value{{2, 4, 8}};

using result_t = decltype(
    std::cw<&array_member_owner::values>(std::cw<array_member_value>));

#if defined(_MSC_VER)
static_assert(std::is_lvalue_reference_v<result_t>);
static_assert(std::is_array_v<std::remove_reference_t<result_t>>);
#else
static_assert(!std::is_reference_v<result_t>);
static_assert(std::is_same_v<typename result_t::value_type, const int*>);
static_assert(result_t::value[2] == 8);
#endif

int main() {}
