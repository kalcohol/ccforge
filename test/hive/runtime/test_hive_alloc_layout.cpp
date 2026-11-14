#include <gtest/gtest.h>

#include <hive>

#include <cstddef>
#include <memory>
#include <type_traits>

#if defined(FORGE_HIVE_NO_UNIQUE_ADDRESS)
#error Hive's private attribute macro must not leak
#endif

namespace {

#if defined(__has_cpp_attribute)
#  if __has_cpp_attribute(msvc::no_unique_address) || __has_cpp_attribute(no_unique_address)
constexpr bool has_overlap_attribute = true;
#  else
constexpr bool has_overlap_attribute = false;
#  endif
#else
constexpr bool has_overlap_attribute = false;
#endif

template<class T>
struct empty_allocator final {
    using value_type = T;

    empty_allocator() = default;
    template<class U>
    empty_allocator(const empty_allocator<U>&) noexcept {}

    T* allocate(std::size_t count) { return std::allocator<T>{}.allocate(count); }
    void deallocate(T* allocation, std::size_t count) noexcept {
        std::allocator<T>{}.deallocate(allocation, count);
    }
    template<class U>
    bool operator==(const empty_allocator<U>&) const noexcept { return true; }
};

template<class T>
struct pointer_allocator {
    using value_type = T;
    void* state = nullptr;

    pointer_allocator() = default;
    template<class U>
    pointer_allocator(const pointer_allocator<U>& other) noexcept : state(other.state) {}

    T* allocate(std::size_t count) { return std::allocator<T>{}.allocate(count); }
    void deallocate(T* allocation, std::size_t count) noexcept {
        std::allocator<T>{}.deallocate(allocation, count);
    }
    template<class U>
    bool operator==(const pointer_allocator<U>& other) const noexcept {
        return state == other.state;
    }
};

TEST(HiveAllocLayout, EmptyAllocatorAddsNoStorage) {
    using standard_hive = std::hive<int>;
    using empty_hive = std::hive<int, empty_allocator<int>>;
    using stateful_hive = std::hive<int, pointer_allocator<int>>;
    static_assert(std::is_empty_v<empty_allocator<int>>);
    static_assert(sizeof(pointer_allocator<int>) == sizeof(void*));

    if (!has_overlap_attribute) {
        GTEST_SKIP() << "Compiler has no potentially-overlapping member attribute";
    }

    // Limit the exact delta to the common LP64 and Windows x64 layouts.
    // Both leave no tail padding after limits, control pointer and epoch.
    if constexpr (sizeof(void*) == 8 && alignof(void*) == 8 &&
                  sizeof(std::size_t) == 8 && alignof(std::size_t) == 8) {
        EXPECT_EQ(sizeof(standard_hive), sizeof(empty_hive));
        EXPECT_EQ(sizeof(stateful_hive), sizeof(empty_hive) + sizeof(pointer_allocator<int>));
    } else {
        GTEST_SKIP() << "Exact allocator size delta requires a conventional 64-bit layout";
    }
}

} // namespace
