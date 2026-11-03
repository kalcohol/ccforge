#include <mdspan>
#include <concepts>
#include <type_traits>
#include <utility>

#if !defined(__cpp_lib_submdspan) || __cpp_lib_submdspan < 202603L
#error This probe requires the current submdspan slice types
#endif

template<class T>
constexpr bool all_slice_fields() {
    return sizeof(std::extent_slice<T, int, int>) > 0 &&
           sizeof(std::extent_slice<int, T, int>) > 0 &&
           sizeof(std::extent_slice<int, int, T>) > 0 &&
           sizeof(std::range_slice<T, int, int>) > 0 &&
           sizeof(std::range_slice<int, T, int>) > 0 &&
           sizeof(std::range_slice<int, int, T>) > 0;
}

template<class T>
struct constant_without_value_type {
    static constexpr T value = T(1);
    constexpr operator T() const noexcept { return value; }
};

struct reference_constant_without_value_type {
    static constexpr int storage = 1;
    static constexpr const int& value = storage;
    constexpr operator const int&() const noexcept { return value; }
};

template<class T>
concept has_value_type = requires { typename T::value_type; };

static_assert(!has_value_type<constant_without_value_type<int>>);
static_assert(!has_value_type<reference_constant_without_value_type>);
static_assert(all_slice_fields<signed char>());
static_assert(all_slice_fields<unsigned char>());
static_assert(all_slice_fields<short>());
static_assert(all_slice_fields<unsigned short>());
static_assert(all_slice_fields<int>());
static_assert(all_slice_fields<unsigned int>());
static_assert(all_slice_fields<long>());
static_assert(all_slice_fields<unsigned long>());
static_assert(all_slice_fields<long long>());
static_assert(all_slice_fields<unsigned long long>());
static_assert(all_slice_fields<const int>());
static_assert(all_slice_fields<volatile unsigned>());
static_assert(all_slice_fields<constant_without_value_type<int>>());
static_assert(all_slice_fields<constant_without_value_type<unsigned>>());
static_assert(all_slice_fields<reference_constant_without_value_type>());
static_assert(all_slice_fields<std::integral_constant<int, 1>>());
static_assert(all_slice_fields<std::constant_wrapper<1>>());
static_assert(all_slice_fields<std::constant_wrapper<1zu>>());
// Character-valued constants model integral-constant-like, unlike direct characters.
static_assert(all_slice_fields<constant_without_value_type<char>>());
static_assert(all_slice_fields<constant_without_value_type<wchar_t>>());
static_assert(all_slice_fields<constant_without_value_type<char8_t>>());
static_assert(all_slice_fields<constant_without_value_type<char16_t>>());
static_assert(all_slice_fields<constant_without_value_type<char32_t>>());
static_assert(all_slice_fields<std::constant_wrapper<char{1}>>());

static_assert(std::is_aggregate_v<std::extent_slice<int, int, int>>);
static_assert(std::is_aggregate_v<std::range_slice<int, int>>);
constexpr auto extent = std::extent_slice{0, 2u, 1};
constexpr auto range = std::range_slice{0, 2};
static_assert(extent.offset == 0 && extent.extent == 2 && extent.stride == 1);
static_assert(range.first == 0 && range.last == 2 && range.stride == 1);

int main() { return 0; }
