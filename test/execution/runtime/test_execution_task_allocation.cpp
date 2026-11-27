// Frame allocation of std::execution::task ([task.promise]/13 to /19).
// Test names carry the pilot acceptance case they cover (G1-01 to G1-04).
//
// A compiler may elide a coroutine frame allocation, and nothing a test can
// observe says whether it did. The coroutine cases therefore accept zero or
// one allocation per frame and require, in every build mode, that whatever
// was allocated went to the expected identity and was released there with
// the same pointer and count. The allocation functions are also called
// directly, which no elision can remove.

#include <execution>

// Sanitizer runtimes own the global allocation replacements.
#if defined(__SANITIZE_THREAD__) || defined(__SANITIZE_ADDRESS__)
#define FORGE_TEST_TASK_ALLOCATION_SANITIZED 1
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer) || __has_feature(address_sanitizer)
#define FORGE_TEST_TASK_ALLOCATION_SANITIZED 1
#endif
#endif

#if !defined(FORGE_TEST_TASK_ALLOCATION_SANITIZED)
#include "global_allocation_probe.hpp"
#endif

#include <gtest/gtest.h>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

namespace ex = std::execution;

constexpr std::size_t unit = __STDCPP_DEFAULT_NEW_ALIGNMENT__;

// ---------------------------------------------------------------------------
// An allocator with a distinct, observable identity

struct arena {
    struct block {
        void* pointer = nullptr;
        void* raw = nullptr;
        std::size_t count = 0, raw_count = 0, size = 0, alignment = 0;
        bool live = false;
    };

    explicit arena(int id) : id(id) {}
    arena(const arena&) = delete;

    unsigned live() const {
        return static_cast<unsigned>(std::count_if(blocks.begin(), blocks.end(),
                                                   [](const block& item) { return item.live; }));
    }

    // Balanced, with the same pointer, count and unit on both sides.
    void expect_balanced() const {
        EXPECT_EQ(allocations, releases);
        EXPECT_EQ(live(), 0u);
        EXPECT_FALSE(wrong_release);
    }

    // At most `frames` allocations since `since`, each in frame units.
    void expect_at_most(unsigned frames, unsigned since = 0) const {
        EXPECT_LE(allocations, since + frames);
        for (const auto& item : blocks) {
            EXPECT_EQ(item.size, unit);
            EXPECT_EQ(item.alignment, unit);
        }
    }

    void expect_untouched() const {
        EXPECT_EQ(attempts, 0u);
        EXPECT_EQ(allocations, 0u);
        EXPECT_EQ(releases, 0u);
    }

    const int id;
    std::vector<block> blocks;
    // Addresses of the live allocator objects rebound to the frame unit.
    std::vector<const void*> unit_allocators;
    unsigned attempts = 0, allocations = 0, releases = 0;
    std::size_t largest_request = 0;
    bool fail = false, wrong_release = false;
    // When base_modulus is set, every block starts at an address congruent
    // to base_residue modulo base_modulus.
    std::size_t base_modulus = 0, base_residue = 0;
    // How the rebound allocator was made from the allocator named at the call.
    unsigned rebinds_from_mutable = 0, rebinds_from_const = 0;
};

template<class T, std::size_t Alignment = alignof(void*)>
struct arena_allocator {
    using value_type = T;
    template<class U>
    struct rebind {
        using other = arena_allocator<U, Alignment>;
    };

    explicit arena_allocator(arena& home) noexcept : home(&home) { enter(); }
    arena_allocator(const arena_allocator& other) noexcept : home(other.home) { enter(); }
    // [task.promise]/16 initializes the rebound allocator from the named
    // parameter, which is a mutable lvalue. Same-type copies are not rebinds.
    template<class U>
        requires (!std::same_as<U, T>)
    arena_allocator(arena_allocator<U, Alignment>& other) noexcept : home(other.home) {
        ++home->rebinds_from_mutable;
        enter();
    }
    template<class U>
        requires (!std::same_as<U, T>)
    arena_allocator(const arena_allocator<U, Alignment>& other) noexcept : home(other.home) {
        ++home->rebinds_from_const;
        enter();
    }
    arena_allocator& operator=(const arena_allocator&) = delete;
    ~arena_allocator() {
        if (tracked()) std::erase(home->unit_allocators, static_cast<const void*>(this));
    }

    T* allocate(std::size_t count) {
        ++home->attempts;
        home->largest_request = std::max(home->largest_request, count);
        // Refuse rather than try a request no test means to satisfy.
        if (home->fail || count > (std::size_t{1} << 20)) throw std::bad_alloc{};
        // Over-allocate when a test fixes the block's address residue.
        const std::size_t spare = home->base_modulus != 0 ? home->base_modulus / sizeof(T) : 0;
        T* const raw = std::allocator<T>{}.allocate(count + spare);
        T* pointer = raw;
        if (home->base_modulus != 0) {
            while (reinterpret_cast<std::uintptr_t>(pointer) % home->base_modulus != home->base_residue) {
                ++pointer;
            }
        }
        home->blocks.push_back({pointer, raw, count, count + spare, sizeof(T), alignof(T), true});
        ++home->allocations;
        return pointer;
    }

    void deallocate(T* pointer, std::size_t count) noexcept {
        ++home->releases;
        for (auto& item : home->blocks) {
            if (!item.live || item.pointer != pointer) continue;
            home->wrong_release |=
                item.count != count || item.size != sizeof(T) || item.alignment != alignof(T);
            item.live = false;
            std::allocator<T>{}.deallocate(static_cast<T*>(item.raw), item.raw_count);
            return;
        }
        home->wrong_release = true;
    }

    template<class U>
    bool operator==(const arena_allocator<U, Alignment>& other) const noexcept {
        return home == other.home;
    }

    static constexpr bool tracked() { return sizeof(T) == unit && alignof(T) == unit; }
    void enter() {
        if (tracked()) home->unit_allocators.push_back(this);
    }

    alignas(Alignment) arena* home;
};

static_assert(alignof(arena_allocator<int, 64>) == 64);

// The task's own allocator_type: default constructible, and its identity is
// the default arena.
arena* default_home = nullptr;

template<class T>
struct default_arena_allocator : arena_allocator<T> {
    template<class U>
    struct rebind {
        using other = default_arena_allocator<U>;
    };

    default_arena_allocator() noexcept : arena_allocator<T>(*default_home) {}
    default_arena_allocator(const default_arena_allocator&) noexcept = default;
    template<class U>
    default_arena_allocator(default_arena_allocator<U>& other) noexcept
        : arena_allocator<T>(static_cast<arena_allocator<U>&>(other)) {}
    template<class U>
    default_arena_allocator(const default_arena_allocator<U>& other) noexcept
        : arena_allocator<T>(static_cast<const arena_allocator<U>&>(other)) {}
};

struct arena_environment {
    using start_scheduler_type = ex::inline_scheduler;
    using allocator_type = default_arena_allocator<std::byte>;
};

struct inline_environment {
    using start_scheduler_type = ex::inline_scheduler;
};

static_assert(std::same_as<ex::task<int, arena_environment>::allocator_type,
                           default_arena_allocator<std::byte>>);
static_assert(std::same_as<ex::task<int, inline_environment>::allocator_type, std::allocator<std::byte>>);

// Installs a fresh default arena for one test.
struct default_arena_scope {
    default_arena_scope() { default_home = &home; }
    ~default_arena_scope() { default_home = nullptr; }
    arena home{0};
};

// The receiver's ordinary and scheduler allocators are distinct identities
// that frame allocation must leave alone.
struct result_receiver {
    using receiver_concept = ex::receiver_tag;

    auto get_env() const noexcept {
        return ex::env{ex::prop{std::get_allocator, arena_allocator<std::byte>{*ordinary}},
                       ex::prop{std::get_scheduler_allocator, arena_allocator<std::byte>{*scheduling}}};
    }

    void set_value(int result) && noexcept {
        *value = result;
        // The frame is released before the receiver is entered.
        *frames_live_at_entry = watched ? static_cast<int>(watched->live()) : -1;
    }
    void set_error(std::exception_ptr) && noexcept { *value = -1; }
    void set_stopped() && noexcept { *value = -2; }

    std::optional<int>* value;
    arena* ordinary;
    arena* scheduling;
    arena* watched = nullptr;
    int* frames_live_at_entry = nullptr;
};

struct run_state {
    std::optional<int> value;
    arena ordinary{90};
    arena scheduling{91};
    int frames_live_at_entry = -1;

    result_receiver receiver(arena* watched = nullptr) {
        return {&value, &ordinary, &scheduling, watched, &frames_live_at_entry};
    }

    void expect_other_allocators_untouched() const {
        ordinary.expect_untouched();
        scheduling.expect_untouched();
    }
};

// ---------------------------------------------------------------------------
// G1-01: default, leading allocator_arg and member forms

int bodies = 0;

ex::task<int, arena_environment> default_frame(int result) {
    ++bodies;
    co_return result;
}

// The explicit allocator's type differs from the task's allocator_type.
ex::task<int, arena_environment> explicit_frame(std::allocator_arg_t, arena_allocator<int>, int result) {
    ++bodies;
    co_return result;
}

struct owner {
    ex::task<int, arena_environment> member_frame(std::allocator_arg_t, arena_allocator<long>, int result) {
        ++bodies;
        co_return result + bias;
    }

    ex::task<int, arena_environment> member_default_frame(int result) {
        ++bodies;
        co_return result + bias;
    }

    int bias;
};

enum class lifetime { unconnected, unstarted, completed };

// Runs one task through the chosen lifetime. Its frame, if it was allocated
// at all, belongs to frame_home alone and is released there.
template<class MakeTask>
void exercise(lifetime how, arena& frame_home, std::vector<arena*> idle, int expected, MakeTask make) {
    SCOPED_TRACE(static_cast<int>(how));
    run_state state;
    bodies = 0;
    {
        auto task = make();
        EXPECT_EQ(bodies, 0);
        frame_home.expect_at_most(1);
        // A frame that exists is still held.
        EXPECT_EQ(frame_home.releases, 0u);
        EXPECT_EQ(frame_home.live(), frame_home.allocations);
        for (const arena* other : idle) other->expect_untouched();
        if (how != lifetime::unconnected) {
            auto operation = ex::connect(std::move(task), state.receiver(&frame_home));
            EXPECT_EQ(frame_home.releases, 0u);
            if (how == lifetime::completed) {
                ex::start(operation);
                EXPECT_EQ(state.value, expected);
                EXPECT_EQ(state.frames_live_at_entry, 0);
                // The retained operation holds no frame.
                frame_home.expect_balanced();
            }
        }
    }
    EXPECT_EQ(bodies, how == lifetime::completed ? 1 : 0);
    frame_home.expect_at_most(1);
    frame_home.expect_balanced();
    for (const arena* other : idle) other->expect_untouched();
    state.expect_other_allocators_untouched();
}

TEST(ExecutionTaskAllocation, G101_DefaultFrameUsesTheTaskAllocatorType) {
    for (auto how : {lifetime::unconnected, lifetime::unstarted, lifetime::completed}) {
        default_arena_scope defaults;
        arena explicit_home{1};
        exercise(how, defaults.home, {&explicit_home}, 5, [] { return default_frame(5); });
    }
}

TEST(ExecutionTaskAllocation, G101_LeadingAllocatorArgSelectsThatIdentity) {
    for (auto how : {lifetime::unconnected, lifetime::unstarted, lifetime::completed}) {
        default_arena_scope defaults;
        arena first{1}, second{2};
        exercise(how, second, {&defaults.home, &first}, 6, [&] {
            return explicit_frame(std::allocator_arg, arena_allocator<int>{second}, 6);
        });
    }
}

TEST(ExecutionTaskAllocation, G101_MemberCoroutineTakesTheAllocatorAfterTheObject) {
    for (auto how : {lifetime::unconnected, lifetime::unstarted, lifetime::completed}) {
        default_arena_scope defaults;
        arena first{1}, second{2};
        owner object{100};
        exercise(how, first, {&defaults.home, &second}, 107, [&] {
            return object.member_frame(std::allocator_arg, arena_allocator<long>{first}, 7);
        });
    }
    {
        // Without allocator_arg a member coroutine takes the default.
        default_arena_scope defaults;
        arena first{1};
        owner object{100};
        exercise(lifetime::completed, defaults.home, {&first}, 108,
                 [&] { return object.member_default_frame(8); });
    }
}

ex::task<int, inline_environment> standard_default_frame(int result) { co_return result; }

TEST(ExecutionTaskAllocation, G101_StandardDefaultAllocatorCompletes) {
    run_state state;
#if !defined(FORGE_TEST_TASK_ALLOCATION_SANITIZED)
    allocation_probe::calls = 0;
    allocation_probe::enabled = true;
#endif
    {
        auto task = standard_default_frame(9);
#if !defined(FORGE_TEST_TASK_ALLOCATION_SANITIZED)
        allocation_probe::enabled = false;
        // Diagnostic only: allocator<byte> rebound to the unit reaches the
        // global operator new unless the frame was elided.
        RecordProperty("global_new_calls_for_default_frame",
                       static_cast<int>(allocation_probe::calls.load()));
#endif
        auto operation = ex::connect(std::move(task), state.receiver());
        ex::start(operation);
    }
    EXPECT_EQ(state.value, 9);
    state.expect_other_allocators_untouched();
}

// ---------------------------------------------------------------------------
// G1-02: the allocation functions themselves

using promise = ex::task<int, arena_environment>::promise_type;
// What sized delete needs next to the allocator: a release function pointer
// and the pointer the allocator returned.
struct release_header {
    void (*release)(unsigned char*, std::size_t) noexcept;
    void* origin;
};

constexpr std::size_t round_up(std::size_t value, std::size_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

// Where the frame ends and the release header has been placed.
constexpr std::size_t after_release(std::size_t frame_size) {
    return round_up(frame_size, alignof(release_header)) + sizeof(release_header);
}

// The least number of units for a frame followed by what sized delete
// needs: the release header and the rebound allocator, each aligned.
//
// An allocator aligned no more strictly than the unit sits at the next
// offset aligned for it. One aligned to A > unit cannot be placed by offset
// in a block that is only unit-aligned. From the next unit boundary the
// distance to an A-aligned address is at most A - unit, and that worst case
// occurs, so a count that works for every block address needs that much.
template<class Allocator>
constexpr std::size_t least_units(std::size_t frame_size) {
    constexpr std::size_t alignment = alignof(Allocator);
    if (alignment <= unit) {
        return round_up(round_up(after_release(frame_size), alignment) + sizeof(Allocator), unit) / unit;
    }
    return round_up(round_up(after_release(frame_size), unit) + (alignment - unit) + sizeof(Allocator), unit) /
           unit;
}

template<class Allocator, class Call>
void check_direct_allocation(arena& home, std::size_t frame_size, Call allocate) {
    SCOPED_TRACE(frame_size);
    constexpr std::size_t alignment = alignof(Allocator);
    const unsigned before = home.allocations;
    void* const pointer = allocate(frame_size);
    ASSERT_EQ(home.allocations, before + 1);
    const arena::block granted = home.blocks.back();
    // [task.promise]/14, /16 and /17.
    EXPECT_EQ(pointer, granted.pointer);
    EXPECT_EQ(granted.size, unit);
    EXPECT_EQ(granted.alignment, unit);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(pointer) % unit, 0u);
    EXPECT_EQ(granted.count, least_units<Allocator>(frame_size));
    // The one allocator kept for delete lies behind the release function
    // pointer, inside the block, at an address aligned for it.
    ASSERT_EQ(home.unit_allocators.size(), 1u);
    const auto stored = reinterpret_cast<std::uintptr_t>(home.unit_allocators.front());
    const auto begin = reinterpret_cast<std::uintptr_t>(pointer);
    EXPECT_EQ(stored % alignment, 0u);
    EXPECT_GE(stored, begin + after_release(frame_size));
    EXPECT_LT(stored, begin + after_release(frame_size) + std::max(alignment, unit));
    EXPECT_LE(stored + sizeof(Allocator), begin + granted.count * unit);
    // The frame bytes are usable.
    std::fill_n(static_cast<unsigned char*>(pointer), frame_size, static_cast<unsigned char>(0x5a));
    // [task.promise]/18 and /19: the same pointer and size come back.
    promise::operator delete(pointer, frame_size);
    EXPECT_TRUE(home.unit_allocators.empty());
    home.expect_balanced();
}

TEST(ExecutionTaskAllocation, G102_UnitCountIsMinimalAcrossUnitBoundaries) {
    using rebound = arena_allocator<std::max_align_t>;
    static_assert(sizeof(rebound) == sizeof(arena_allocator<int>));
    arena home{3};
    std::size_t previous = 0;
    bool grew = false;
    for (std::size_t frame_size = 1; frame_size <= 4 * unit + 1; ++frame_size) {
        check_direct_allocation<rebound>(home, frame_size, [&](std::size_t size) {
            return promise::operator new(size, std::allocator_arg, arena_allocator<int>{home});
        });
        const std::size_t count = home.blocks.back().count;
        if (previous != 0) {
            EXPECT_GE(count, previous);
            EXPECT_LE(count, previous + 1);
            grew |= count == previous + 1;
        }
        previous = count;
    }
    // The sweep crossed at least one unit boundary.
    EXPECT_TRUE(grew);
}

TEST(ExecutionTaskAllocation, G102_ImplicitObjectFormAndDefaultFormAllocateAlike) {
    using rebound = arena_allocator<std::max_align_t>;
    default_arena_scope defaults;
    arena home{4};
    owner object{0};
    for (std::size_t frame_size : {unit - 1, unit, unit + 1, 3 * unit}) {
        check_direct_allocation<rebound>(home, frame_size, [&](std::size_t size) {
            return promise::operator new(size, object, std::allocator_arg, arena_allocator<long>{home}, 7);
        });
        check_direct_allocation<default_arena_allocator<std::max_align_t>>(
            defaults.home, frame_size, [&](std::size_t size) { return promise::operator new(size); });
    }
}

TEST(ExecutionTaskAllocation, G102_RebindUsesTheNamedMutableAllocator) {
    arena home{5};
    // The named parameter is copied from the argument, then converted once
    // as a mutable lvalue. No const conversion and no second conversion.
    void* const pointer = promise::operator new(40, std::allocator_arg, arena_allocator<int>{home});
    EXPECT_EQ(home.rebinds_from_mutable, 1u);
    EXPECT_EQ(home.rebinds_from_const, 0u);
    promise::operator delete(pointer, 40);
    EXPECT_EQ(home.rebinds_from_mutable, 1u);
    EXPECT_EQ(home.rebinds_from_const, 0u);
    home.expect_balanced();

    owner object{0};
    const arena_allocator<long> named{home};
    void* const second = promise::operator new(40, object, std::allocator_arg, named);
    EXPECT_EQ(home.rebinds_from_mutable, 2u);
    EXPECT_EQ(home.rebinds_from_const, 0u);
    promise::operator delete(second, 40);
    home.expect_balanced();
}

TEST(ExecutionTaskAllocation, G102_OverAlignedAllocatorCountIsExactForEveryBase) {
    using wide = arena_allocator<std::max_align_t, 64>;
    constexpr std::size_t alignment = alignof(wide);
    static_assert(alignment == 64 && alignment > unit);
    // Every residue a unit-aligned block can have modulo the allocator's
    // alignment. The residue `unit` is the worst: the first unit boundary
    // behind the release function is then as far as it can be from an
    // aligned address.
    for (std::size_t residue = 0; residue < alignment; residue += unit) {
        SCOPED_TRACE(residue);
        arena home{6};
        home.base_modulus = alignment;
        home.base_residue = residue;
        for (std::size_t frame_size = 1; frame_size <= 6 * unit; frame_size += 3) {
            check_direct_allocation<wide>(home, frame_size, [&](std::size_t size) {
                return promise::operator new(size, std::allocator_arg, arena_allocator<int, 64>{home});
            });
            EXPECT_EQ(reinterpret_cast<std::uintptr_t>(home.blocks.back().pointer) % alignment, residue);
        }
    }
    // With the worst base the stored allocator ends exactly where the least
    // count ends for some frame size: no unit is spare.
    arena home{7};
    home.base_modulus = alignment;
    home.base_residue = unit % alignment;
    bool tight = false;
    for (std::size_t frame_size = 1; frame_size <= 8 * unit; ++frame_size) {
        void* const pointer =
            promise::operator new(frame_size, std::allocator_arg, arena_allocator<int, 64>{home});
        const auto stored = reinterpret_cast<std::uintptr_t>(home.unit_allocators.front());
        const auto end = reinterpret_cast<std::uintptr_t>(pointer) + home.blocks.back().count * unit;
        tight |= end - (stored + sizeof(wide)) < unit;
        promise::operator delete(pointer, frame_size);
    }
    EXPECT_TRUE(tight);
    home.expect_balanced();
}

// An allocator whose size_type is narrower than size_t. A unit count it
// cannot represent must be refused, not truncated into a smaller request.
struct narrow_log {
    unsigned attempts = 0, allocations = 0, releases = 0;
    std::size_t last_count = 0;
    bool wrong_release = false;
};

template<class T>
struct narrow_allocator {
    using value_type = T;
    using size_type = unsigned char;
    using difference_type = std::ptrdiff_t;

    explicit narrow_allocator(narrow_log& log) noexcept : log(&log) {}
    template<class U>
    narrow_allocator(const narrow_allocator<U>& other) noexcept : log(other.log) {}

    T* allocate(size_type count) {
        ++log->attempts;
        ++log->allocations;
        log->last_count = count;
        return std::allocator<T>{}.allocate(count);
    }
    void deallocate(T* pointer, size_type count) noexcept {
        ++log->releases;
        log->wrong_release |= count != log->last_count;
        std::allocator<T>{}.deallocate(pointer, count);
    }

    template<class U>
    bool operator==(const narrow_allocator<U>& other) const noexcept { return log == other.log; }

    narrow_log* log;
};

TEST(ExecutionTaskAllocation, G102_UnitCountBeyondTheAllocatorSizeTypeIsRefused) {
    using rebound = narrow_allocator<std::max_align_t>;
    // allocator_traits::max_size for this allocator, in units.
    constexpr std::size_t most = std::numeric_limits<unsigned char>::max() / unit;
    narrow_log log;
    // The largest frame whose count is exactly `most`.
    std::size_t fitting = 0;
    for (std::size_t frame_size = 1; least_units<rebound>(frame_size) <= most; ++frame_size) {
        fitting = frame_size;
    }
    ASSERT_GT(fitting, 0u);
    ASSERT_EQ(least_units<rebound>(fitting), most);
    ASSERT_EQ(least_units<rebound>(fitting + 1), most + 1);

    void* const pointer = promise::operator new(fitting, std::allocator_arg, narrow_allocator<int>{log});
    EXPECT_EQ(log.last_count, most);
    promise::operator delete(pointer, fitting);
    EXPECT_EQ(log.allocations, 1u);
    EXPECT_EQ(log.releases, 1u);
    EXPECT_FALSE(log.wrong_release);

    // One unit more, and counts that would truncate to something small.
    const unsigned attempts = log.attempts;
    for (std::size_t frame_size : {fitting + 1, 255 * unit, 256 * unit, 257 * unit, 4096 * unit}) {
        SCOPED_TRACE(frame_size);
        EXPECT_THROW((void)promise::operator new(frame_size, std::allocator_arg, narrow_allocator<int>{log}),
                     std::bad_array_new_length);
    }
    EXPECT_EQ(log.attempts, attempts);
}

// ---------------------------------------------------------------------------
// G1-03: failure and rollback

TEST(ExecutionTaskAllocation, G103_AllocationFailureLeavesNothingBehind) {
    default_arena_scope defaults;
    arena home{8};
    owner object{0};
    // The allocation functions are called directly: no elision can skip them.
    home.fail = true;
    EXPECT_THROW((void)promise::operator new(48, std::allocator_arg, arena_allocator<int>{home}),
                 std::bad_alloc);
    EXPECT_THROW((void)promise::operator new(48, object, std::allocator_arg, arena_allocator<long>{home}),
                 std::bad_alloc);
    EXPECT_EQ(home.attempts, 2u);
    EXPECT_EQ(home.allocations, 0u);
    EXPECT_EQ(home.releases, 0u);
    EXPECT_TRUE(home.unit_allocators.empty());
    defaults.home.expect_untouched();

    defaults.home.fail = true;
    EXPECT_THROW((void)promise::operator new(48), std::bad_alloc);
    EXPECT_EQ(defaults.home.attempts, 1u);
    EXPECT_EQ(defaults.home.allocations, 0u);
    EXPECT_EQ(defaults.home.releases, 0u);
    EXPECT_TRUE(defaults.home.unit_allocators.empty());

    // Through a coroutine call: either the frame was elided and the task
    // exists, or the failure propagates before the body is entered.
    bodies = 0;
    std::optional<ex::task<int, arena_environment>> escaped;
    try {
        escaped.emplace(explicit_frame(std::allocator_arg, arena_allocator<int>{home}, 1));
    } catch (const std::bad_alloc&) {
    }
    EXPECT_EQ(bodies, 0);
    escaped.reset();
    EXPECT_EQ(home.allocations, 0u);
    EXPECT_EQ(home.releases, 0u);
}

// A coroutine parameter with an observable identity and lifetime. Object 1
// is the caller's parameter. The coroutine may copy it into the frame as
// object 2, and that copy can be made to throw after the frame was acquired.
// The draft also lets the copy be omitted, independently of frame elision,
// so the body reports which object it actually uses.
struct fragile_counts {
    int live = 0, constructed = 0;
    bool fail_copies = false;
    int used_by_body = 0;
};

struct fragile {
    explicit fragile(fragile_counts& counts) : counts(&counts) { arrive(); }
    fragile(const fragile& other) : counts(other.counts) {
        if (counts->fail_copies) throw std::runtime_error("parameter copy");
        arrive();
    }
    ~fragile() { --counts->live; }

    void arrive() {
        ++counts->live;
        id = ++counts->constructed;
    }

    fragile_counts* counts;
    int id = 0;
};

ex::task<int, arena_environment> takes_fragile(std::allocator_arg_t, arena_allocator<int>, fragile parameter) {
    ++bodies;
    parameter.counts->used_by_body = parameter.id;
    co_return parameter.id;
}

// Defined for one recorded qualification build: the toolchain is then known
// to allocate the frame and to copy the parameter, and the throwing path
// must show one acquisition followed by exactly one matching release.
#if defined(FORGE_TEST_TASK_ALLOCATION_REQUIRE_FRAMES)
constexpr bool frames_are_required = true;
#else
constexpr bool frames_are_required = false;
#endif

TEST(ExecutionTaskAllocation, G103_ThrowingParameterCopyReleasesTheFrameOnce) {
    default_arena_scope defaults;
    arena home{9};
    fragile_counts counts;
    bodies = 0;
    // The task would escape into this slot. The argument is a prvalue, so
    // the only possible copy is the one into the frame.
    std::optional<ex::task<int, arena_environment>> escaped;
    counts.fail_copies = true;
    bool threw = false;
    try {
        escaped.emplace(takes_fragile(std::allocator_arg, arena_allocator<int>{home}, fragile{counts}));
    } catch (const std::runtime_error&) {
        threw = true;
    }
    // Either the copy into the frame threw, or the compiler omitted it.
    EXPECT_EQ(threw, !escaped.has_value());
    EXPECT_EQ(bodies, 0);
    EXPECT_EQ(counts.used_by_body, 0);
    // Only the caller's parameter was ever built.
    EXPECT_EQ(counts.constructed, 1);
    if (threw) {
        EXPECT_EQ(counts.live, 0);
        // A frame that was acquired was released once, through the same
        // arena, with the same pointer and count.
        home.expect_at_most(1);
        home.expect_balanced();
        EXPECT_TRUE(home.unit_allocators.empty());
    }
    if (frames_are_required) {
        EXPECT_TRUE(threw);
        ASSERT_EQ(home.allocations, 1u);
        EXPECT_EQ(home.releases, 1u);
        EXPECT_FALSE(home.blocks.front().live);
        EXPECT_FALSE(home.wrong_release);
    }
    escaped.reset();
    EXPECT_EQ(counts.live, 0);
    home.expect_balanced();
    defaults.home.expect_untouched();

    // The control: the same call with a quiet parameter runs to completion.
    arena control{10};
    fragile_counts quiet;
    run_state state;
    {
        escaped.emplace(takes_fragile(std::allocator_arg, arena_allocator<int>{control}, fragile{quiet}));
        // The frame holds object 2, or object 1 was not copied at all.
        EXPECT_GE(quiet.constructed, 1);
        EXPECT_LE(quiet.constructed, 2);
        auto operation = ex::connect(std::move(*escaped), state.receiver(&control));
        ex::start(operation);
        escaped.reset();
    }
    EXPECT_EQ(bodies, 1);
    // The body used the last object built, and the task returned its id.
    EXPECT_EQ(quiet.used_by_body, quiet.constructed);
    EXPECT_EQ(state.value, quiet.constructed);
    EXPECT_EQ(quiet.live, 0);
    control.expect_at_most(1);
    control.expect_balanced();
    if (frames_are_required) {
        EXPECT_EQ(quiet.constructed, 2);
        EXPECT_EQ(control.allocations, 1u);
    }
}

TEST(ExecutionTaskAllocation, G103_OversizedRequestsDoNotWrap) {
    constexpr std::size_t top = std::numeric_limits<std::size_t>::max();
    arena home{11};
    // Too large to describe: no allocation is attempted at all. The rebound
    // allocator is still initialized from the named one, once, as the first
    // effect of [task.promise]/16.
    unsigned rejected = 0;
    for (std::size_t below : {std::size_t{0}, std::size_t{1}, std::size_t{7}, unit - 1, unit}) {
        SCOPED_TRACE(below);
        EXPECT_THROW((void)promise::operator new(top - below, std::allocator_arg, arena_allocator<int>{home}),
                     std::bad_array_new_length);
        ++rejected;
        EXPECT_EQ(home.rebinds_from_mutable, rejected);
        EXPECT_EQ(home.rebinds_from_const, 0u);
        EXPECT_EQ(home.attempts, 0u);
    }
    EXPECT_EQ(home.attempts, 0u);
    // Large but describable: the allocator is asked for at least the frame.
    for (std::size_t frame_size : {top / 2, top / 4 + 3, top - 64 * unit}) {
        SCOPED_TRACE(frame_size);
        home.largest_request = 0;
        const unsigned before = home.attempts;
        EXPECT_THROW((void)promise::operator new(frame_size, std::allocator_arg, arena_allocator<int>{home}),
                     std::bad_alloc);
        EXPECT_EQ(home.attempts, before + 1);
        EXPECT_GE(home.largest_request, frame_size / unit);
    }
    EXPECT_EQ(home.allocations, 0u);
    EXPECT_EQ(home.releases, 0u);
    EXPECT_TRUE(home.unit_allocators.empty());
}

// ---------------------------------------------------------------------------
// G1-04: a nested task chooses its own frame allocator

ex::task<int, arena_environment> child_default(int result) { co_return result; }

ex::task<int, arena_environment> child_explicit(std::allocator_arg_t, arena_allocator<char>, int result) {
    co_return result;
}

// What the parent hands to its child's coroutine call.
enum class forwarding { none, distinct, own };

// The parent's frame allocator is explicit here. It gives the child nothing,
// a distinct allocator, or its own.
ex::task<int, arena_environment> parent_explicit(std::allocator_arg_t, arena_allocator<int> own,
                                                 forwarding mode, arena* distinct) {
    if (mode == forwarding::own) {
        co_return 10 + co_await child_explicit(std::allocator_arg, arena_allocator<char>{own}, 3);
    }
    if (mode == forwarding::distinct) {
        co_return 10 + co_await child_explicit(std::allocator_arg, arena_allocator<char>{*distinct}, 1);
    }
    co_return 10 + co_await child_default(2);
}

// The parent's frame allocator is the default. Its own allocator is then
// the task's allocator_type, which a child without allocator_arg also uses.
ex::task<int, arena_environment> parent_default(forwarding mode, arena* distinct) {
    if (mode == forwarding::own) {
        co_return 20 + co_await child_explicit(std::allocator_arg, default_arena_allocator<char>{}, 3);
    }
    if (mode == forwarding::distinct) {
        co_return 20 + co_await child_explicit(std::allocator_arg, arena_allocator<char>{*distinct}, 1);
    }
    co_return 20 + co_await child_default(2);
}

TEST(ExecutionTaskAllocation, G104_ChildFollowsItsOwnCallNotItsParent) {
    for (bool parent_is_explicit : {false, true}) {
        for (auto mode : {forwarding::none, forwarding::distinct, forwarding::own}) {
            SCOPED_TRACE(parent_is_explicit);
            SCOPED_TRACE(static_cast<int>(mode));
            default_arena_scope defaults;
            // The parent's explicit identity and the distinct one given to
            // the child are different arenas, so neither can stand in for
            // the other.
            arena parent_home{12}, child_home{13};
            arena& parent_arena = parent_is_explicit ? parent_home : defaults.home;
            arena& child_arena = mode == forwarding::own ? parent_arena
                : mode == forwarding::distinct ? child_home : defaults.home;
            run_state state;
            {
                auto task = parent_is_explicit
                    ? parent_explicit(std::allocator_arg, arena_allocator<int>{parent_home}, mode, &child_home)
                    : parent_default(mode, &child_home);
                // Before start only the parent's frame can exist, and only
                // in the parent's arena.
                parent_arena.expect_at_most(1);
                EXPECT_EQ(parent_arena.live(), parent_arena.allocations);
                for (arena* other : {&parent_home, &child_home, &defaults.home}) {
                    if (other != &parent_arena) other->expect_untouched();
                }
                const unsigned parent_before = parent_arena.allocations;
                const unsigned child_before = child_arena.allocations;

                auto operation = ex::connect(std::move(task), state.receiver());
                ex::start(operation);

                // The child's frame is the only new allocation, and only in
                // the child's arena.
                child_arena.expect_at_most(1, child_before);
                if (&child_arena != &parent_arena) {
                    EXPECT_EQ(parent_arena.allocations, parent_before);
                }
                for (arena* other : {&parent_home, &child_home, &defaults.home}) {
                    if (other != &parent_arena && other != &child_arena) other->expect_untouched();
                }
            }
            const int child_result = mode == forwarding::own ? 3 : mode == forwarding::distinct ? 1 : 2;
            EXPECT_EQ(state.value, (parent_is_explicit ? 10 : 20) + child_result);
            for (arena* each : {&parent_home, &child_home, &defaults.home}) each->expect_balanced();
            // The receiver's ordinary and scheduler allocators are separate.
            state.expect_other_allocators_untouched();
        }
    }
}

} // namespace
