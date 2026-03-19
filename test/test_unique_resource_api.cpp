#include <type_traits>
#include <utility>

#include "../backport/cpp26/unique_resource.hpp"

namespace {

void cleanup_int(int) noexcept {}
void cleanup_pointer(int*) noexcept {}
void cleanup_reference(int&) noexcept {}

using value_resource = std::unique_resource<int, void(*)(int)>;
using pointer_resource = std::unique_resource<int*, void(*)(int*)>;
using reference_resource = std::unique_resource<int&, void(*)(int&)>;
using value_deleter = void(*)(int);

template<class T, class = void>
struct has_reset_with_value : std::false_type {};

template<class T>
struct has_reset_with_value<T, std::void_t<decltype(std::declval<T&>().reset(0))>> : std::true_type {};

template<class T, class = void>
struct has_dereference : std::false_type {};

template<class T>
struct has_dereference<T, std::void_t<decltype(*std::declval<T&>())>> : std::true_type {};

template<class T, class = void>
struct has_arrow : std::false_type {};

template<class T>
struct has_arrow<T, std::void_t<decltype(std::declval<T&>().operator->())>> : std::true_type {};

} // namespace

static_assert(std::is_default_constructible<value_resource>::value,
    "unique_resource should be default constructible");

static_assert(std::is_same<void, decltype(std::declval<value_resource&>().release())>::value,
    "release should return void");

static_assert(std::is_same<const int&, decltype(std::declval<value_resource&>().get())>::value,
    "get should expose only the const overload");

static_assert(std::is_same<const value_deleter&, decltype(std::declval<value_resource&>().get_deleter())>::value,
    "get_deleter should expose only the const overload");

static_assert(has_reset_with_value<value_resource>::value,
    "unique_resource should support reset(resource)");

static_assert(!std::is_default_constructible<reference_resource>::value,
    "reference-backed unique_resource should not be default constructible");

static_assert(std::is_same<int&, decltype(std::declval<const reference_resource&>().get())>::value,
    "reference-backed unique_resource should preserve reference semantics");

static_assert(has_dereference<pointer_resource>::value,
    "pointer-like unique_resource should support operator*");

static_assert(has_arrow<pointer_resource>::value,
    "pointer-like unique_resource should support operator->");

int main() {
    return 0;
}
