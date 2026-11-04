#include <hive>

#include <memory>
#include <type_traits>

namespace {
struct constructor {
    constexpr constructor() noexcept {}
};

template<class T>
struct fixed_allocator : constructor {
    using value_type = T;
    fixed_allocator() = default;
    template<class U> constexpr fixed_allocator(const fixed_allocator<U>&) noexcept {}
    constexpr std::size_t max_size() const noexcept { return 4; }
    T* allocate(std::size_t count) { return std::allocator<T>{}.allocate(count); }
    void deallocate(T* pointer, std::size_t count) noexcept {
        std::allocator<T>{}.deallocate(pointer, count);
    }
    template<class U> bool operator==(const fixed_allocator<U>&) const noexcept { return true; }
};

// Query Hive before any independent allocator_traits constant evaluation.
static_assert(std::hive<int, fixed_allocator<int>>::block_capacity_hard_limits().max == 4);
static_assert(std::hive<int, fixed_allocator<int>>::block_capacity_default_limits().max == 4);
static_assert(!std::is_trivially_default_constructible_v<fixed_allocator<int>>);
}
