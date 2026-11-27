// [task.promise]/15: the allocator rebound to the frame unit must have a
// pointer type as its pointer. Without FORGE_TASK_REJECT_FANCY_POINTER the
// allocator below is the accepted sibling.
//
// The wrapper converts to and from the raw pointer implicitly, so the frame
// allocation code would compile with it. Only the mandate rejects it.

#include <execution>

#include <cstddef>
#include <memory>
#include <type_traits>

namespace ex = std::execution;

template<class T>
struct fancy_pointer {
    fancy_pointer() = default;
    fancy_pointer(std::nullptr_t) noexcept {}
    fancy_pointer(T* raw) noexcept : raw(raw) {}
    template<class U>
        requires std::is_convertible_v<U*, T*>
    fancy_pointer(const fancy_pointer<U>& other) noexcept : raw(other.raw) {}

    operator T*() const noexcept { return raw; }
    std::add_lvalue_reference_t<T> operator*() const noexcept { return *raw; }
    T* operator->() const noexcept { return raw; }

    T* raw = nullptr;
};

template<class T>
struct frame_allocator {
    using value_type = T;
#if defined(FORGE_TASK_REJECT_FANCY_POINTER)
    using pointer = fancy_pointer<T>;
#else
    using pointer = T*;
#endif

    frame_allocator() = default;
    template<class U>
    frame_allocator(const frame_allocator<U>&) noexcept {}

    pointer allocate(std::size_t count) { return pointer(std::allocator<T>{}.allocate(count)); }
    void deallocate(pointer block, std::size_t count) noexcept {
        std::allocator<T>{}.deallocate(static_cast<T*>(block), count);
    }

    template<class U>
    bool operator==(const frame_allocator<U>&) const noexcept { return true; }
};

struct environment {
    using start_scheduler_type = ex::inline_scheduler;
};

using promise = ex::task<void, environment>::promise_type;

int main() {
    void* const frame = promise::operator new(64, std::allocator_arg, frame_allocator<int>{});
    promise::operator delete(frame, 64);
}
