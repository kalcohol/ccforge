// Compile probe: unique_resource API surface and type traits
// T-13: verified through forge::forge target (see CMakeLists.txt)
#include <type_traits>

#include <memory>

namespace {

void cleanup_int(int) {}
void cleanup_pointer(int*) {}
void cleanup_reference(int&) {}
void cleanup_non_assignable(const struct non_assignable_resource&) {}
void cleanup_throwing_assign_reference(struct throwing_assign_reference&) {}

using value_resource = std::unique_resource<int, void(*)(int)>;
using pointer_resource = std::unique_resource<int*, void(*)(int*)>;
using reference_resource = std::unique_resource<int&, void(*)(int&)>;
using value_deleter = void(*)(int);

struct non_assignable_resource {
    int value;

    explicit non_assignable_resource(int initial_value) noexcept
        : value(initial_value) {}

    non_assignable_resource(const non_assignable_resource&) = default;
    non_assignable_resource(non_assignable_resource&&) = default;
    non_assignable_resource& operator=(const non_assignable_resource&) = delete;
    non_assignable_resource& operator=(non_assignable_resource&&) = delete;
};

struct throwing_assign_reference {
    throwing_assign_reference() = default;
    throwing_assign_reference(const throwing_assign_reference&) = default;

    throwing_assign_reference& operator=(const throwing_assign_reference&) noexcept(false) {
        return *this;
    }

    throwing_assign_reference& operator=(throwing_assign_reference&&) noexcept(false) {
        return *this;
    }
};

using non_assignable_value_resource =
    std::unique_resource<non_assignable_resource, void(*)(const non_assignable_resource&)>;
using reference_move_assignment_resource =
    std::unique_resource<throwing_assign_reference&, void(*)(throwing_assign_reference&)>;

template<class T, class RR>
concept resettable_from = requires(T& resource, RR&& value) {
    resource.reset(std::forward<RR>(value));
};

} // namespace

// T-6: use _v suffix throughout; I-2: default constructor removed
static_assert(!std::is_default_constructible_v<value_resource>,
    "unique_resource should not be default constructible");

static_assert(std::is_same_v<void, decltype(std::declval<value_resource&>().release())>,
    "release should return void");

static_assert(std::is_same_v<const int&, decltype(std::declval<value_resource&>().get())>,
    "get should expose only the const overload");

static_assert(std::is_same_v<const value_deleter&, decltype(std::declval<value_resource&>().get_deleter())>,
    "get_deleter should expose only the const overload");

// T-5: use requires expressions instead of SFINAE traits
static_assert(resettable_from<value_resource, int>,
    "unique_resource should support reset(resource)");

static_assert(!resettable_from<non_assignable_value_resource, non_assignable_resource>,
    "reset(RR&&) should be removed from overload resolution when the resource is not assignable");

static_assert(!std::is_default_constructible_v<reference_resource>,
    "reference-backed unique_resource should not be default constructible");

static_assert(std::is_same_v<int&, decltype(std::declval<const reference_resource&>().get())>,
    "reference-backed unique_resource should preserve reference semantics");

static_assert(requires(pointer_resource& r) { *r; },
    "pointer-like unique_resource should support operator*");

static_assert(requires(pointer_resource& r) { r.operator->(); },
    "pointer-like unique_resource should support operator->");

static_assert(noexcept(std::declval<value_resource&>().release()),
    "release should be noexcept");
static_assert(noexcept(std::declval<const value_resource&>().get()),
    "get should be noexcept");
static_assert(noexcept(std::declval<const value_resource&>().get_deleter()),
    "get_deleter should be noexcept");
static_assert(noexcept(std::declval<value_resource&>().reset()),
    "reset() should be noexcept");
static_assert(noexcept(*std::declval<const pointer_resource&>()),
    "operator* should be noexcept");
static_assert(noexcept(std::declval<const pointer_resource&>().operator->()),
    "operator-> should be noexcept");
static_assert(noexcept(std::declval<reference_move_assignment_resource&>() =
                       std::declval<reference_move_assignment_resource&&>()),
    "move assignment noexcept should follow the stored reference_wrapper type");

// T-7: CTAD deduction guide
static_assert(std::is_same_v<
    decltype(std::unique_resource(0, &cleanup_int)),
    std::unique_resource<int, void(*)(int)>>,
    "CTAD should deduce correct types");

// T-11: unique_resource is not copyable
static_assert(!std::is_copy_constructible_v<value_resource>,
    "unique_resource should not be copy constructible");
static_assert(!std::is_copy_assignable_v<value_resource>,
    "unique_resource should not be copy assignable");

int main() {
    return 0;
}
