// MIT License
//
// Copyright (c) 2026 CC Forge Project
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

// Frame storage of std::execution::task ([task.promise]/14 to /19).
//
// One block of default-new-aligned units holds, in this order:
//   - the coroutine frame, at the block's first byte;
//   - a header with a release function pointer and the pointer the allocator
//     returned, at the next address aligned for it;
//   - the rebound allocator, at the next address aligned for it.
// The sized operator delete knows only the pointer and the frame size. It
// finds the header from those two, and the release function, which knows
// the allocator type, finds the allocator and the unit count the same way.
//
// The allocator returns storage for an array of units and creates no
// objects in it ([allocator.members]/3 to /5 for std::allocator). Pointer
// arithmetic is defined only within an array object ([expr.add]/4 and /6),
// and a pointer to the first unit is not a pointer into an array of bytes.
// So a byte array spanning the whole allocation is created in that storage
// first. Every position below is computed from that array's first element.
// The frame, the header and the allocator are then nested within it.

namespace std::execution::__forge_task {

// [task.promise]/14: the unspecified type U.
struct alignas(__STDCPP_DEFAULT_NEW_ALIGNMENT__) __frame_unit {
    unsigned char __bytes[__STDCPP_DEFAULT_NEW_ALIGNMENT__];
};

static_assert(sizeof(__frame_unit) == __STDCPP_DEFAULT_NEW_ALIGNMENT__);
static_assert(alignof(__frame_unit) == __STDCPP_DEFAULT_NEW_ALIGNMENT__);

using __frame_release = void (*)(unsigned char* __base, std::size_t __frame_size) noexcept;

// __origin is the pointer allocate returned, kept for deallocate.
struct __frame_header {
    __frame_release __release_function;
    __frame_unit* __origin;
};

// Offsets are measured from the block's first byte. Every step is checked,
// so an oversized request cannot wrap into a small allocation.
struct __frame_layout {
    std::size_t __header_offset = 0;
    std::size_t __allocator_offset = 0;
    std::size_t __units = 0;
    bool __fits = false;
};

constexpr bool __frame_add(std::size_t __lhs, std::size_t __rhs, std::size_t& __sum) noexcept {
    if (__rhs > static_cast<std::size_t>(-1) - __lhs) {
        return false;
    }
    __sum = __lhs + __rhs;
    return true;
}

constexpr bool __frame_align(std::size_t __offset, std::size_t __alignment, std::size_t& __aligned) noexcept {
    if (!__frame_add(__offset, __alignment - 1, __aligned)) {
        return false;
    }
    __aligned -= __aligned % __alignment;
    return true;
}

constexpr std::size_t __frame_header_offset(std::size_t __frame_size, bool& __fits) noexcept {
    std::size_t __offset = 0;
    __fits = __frame_align(__frame_size, alignof(__frame_header), __offset);
    return __offset;
}

// The block is aligned for a unit. An allocator with a stricter alignment
// gets enough slack to be aligned at its absolute address.
template<class PAlloc>
constexpr __frame_layout __frame_layout_for(std::size_t __frame_size) noexcept {
    constexpr std::size_t __unit = sizeof(__frame_unit);
    constexpr std::size_t __slack = alignof(PAlloc) > __unit ? alignof(PAlloc) - __unit : 0;
    __frame_layout __layout;
    bool __ok = false;
    __layout.__header_offset = __frame_header_offset(__frame_size, __ok);
    std::size_t __total = 0;
    __ok = __ok && __frame_add(__layout.__header_offset, sizeof(__frame_header), __total) &&
           __frame_align(__total, alignof(PAlloc) > __unit ? __unit : alignof(PAlloc),
                         __layout.__allocator_offset) &&
           __frame_add(__layout.__allocator_offset, __slack, __total) &&
           __frame_add(__total, sizeof(PAlloc), __total) &&
           __frame_add(__total, __unit - 1, __total);
    if (__ok) {
        __layout.__units = __total / __unit;
        __layout.__fits = true;
    }
    return __layout;
}

// __base is the first element of the byte array that spans the block. The
// layout reserved enough bytes behind __allocator_offset for the allocator
// at any block address, so std::align always finds its place.
template<class PAlloc>
PAlloc* __frame_allocator_at(unsigned char* __base, const __frame_layout& __layout) noexcept {
    void* __cursor = __base + __layout.__allocator_offset;
    std::size_t __space = __layout.__units * sizeof(__frame_unit) - __layout.__allocator_offset;
    return static_cast<PAlloc*>(std::align(alignof(PAlloc), sizeof(PAlloc), __cursor, __space));
}

inline __frame_header* __frame_header_at(unsigned char* __base, std::size_t __offset) noexcept {
    return static_cast<__frame_header*>(static_cast<void*>(__base + __offset));
}

// [task.promise]/19: deallocate with an allocator equal to the one that
// allocated, and with the pointer it returned and the same unit count.
template<class PAlloc>
void __release_frame(unsigned char* __base, std::size_t __frame_size) noexcept {
    using __traits = std::allocator_traits<PAlloc>;
    const __frame_layout __layout = __frame_layout_for<PAlloc>(__frame_size);
    __frame_unit* const __origin =
        std::launder(__frame_header_at(__base, __layout.__header_offset))->__origin;
    PAlloc* const __stored = std::launder(__frame_allocator_at<PAlloc>(__base, __layout));
    PAlloc __palloc(std::move(*__stored));
    std::destroy_at(__stored);
    __traits::deallocate(__palloc, __origin, static_cast<typename __traits::size_type>(__layout.__units));
}

// [task.promise]/14 to /17. __alloc is the named parameter of operator new,
// a mutable lvalue, and initializes the rebound allocator as /16 says. That
// initialization is the first effect, before any request is refused.
template<class Alloc>
void* __allocate_frame(std::size_t __frame_size, Alloc& __alloc) {
    using __palloc_t = typename std::allocator_traits<Alloc>::template rebind_alloc<__frame_unit>;
    using __traits = std::allocator_traits<__palloc_t>;
    static_assert(std::is_pointer_v<typename __traits::pointer>,
                  "task: the frame allocator's rebound pointer must be a pointer type");

    {
        __palloc_t __palloc(__alloc);
        const __frame_layout __layout = __frame_layout_for<__palloc_t>(__frame_size);
        // The allocator's size_type may be narrower than size_t. A count it
        // cannot represent must not be converted into a smaller request.
        if (__layout.__fits && !std::cmp_greater(__layout.__units, __traits::max_size(__palloc))) {
            const auto __count = static_cast<typename __traits::size_type>(__layout.__units);
            __frame_unit* const __origin = __traits::allocate(__palloc, __count);
            // The byte array that spans the block. Creating it cannot throw.
            unsigned char* const __base = ::new (static_cast<void*>(__origin))
                unsigned char[__layout.__units * sizeof(__frame_unit)];
            // Storing the allocator is the only step after acquisition that
            // can throw. The block then goes back through the allocator that
            // produced it.
            try {
                ::new (static_cast<void*>(__frame_allocator_at<__palloc_t>(__base, __layout)))
                    __palloc_t(__palloc);
            } catch (...) {
                __traits::deallocate(__palloc, __origin, __count);
                throw;
            }
            ::new (static_cast<void*>(__frame_header_at(__base, __layout.__header_offset)))
                __frame_header{&__release_frame<__palloc_t>, __origin};
            return __base;
        }
    }
    // The size cannot be described, or exceeds what the allocator can count.
    throw std::bad_array_new_length();
}

// [task.promise]/18 and /19. __pointer is what operator new returned: the
// address of the byte array's first element, which is still alive.
inline void __deallocate_frame(void* __pointer, std::size_t __frame_size) noexcept {
    unsigned char* const __base = std::launder(static_cast<unsigned char*>(__pointer));
    bool __fits = false;
    const std::size_t __offset = __frame_header_offset(__frame_size, __fits);
    const __frame_release __release_function =
        std::launder(__frame_header_at(__base, __offset))->__release_function;
    __release_function(__base, __frame_size);
}

} // namespace std::execution::__forge_task
