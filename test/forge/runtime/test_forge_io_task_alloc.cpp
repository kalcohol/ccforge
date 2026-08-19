#include <gtest/gtest.h>

#include <forge/io/coro.hpp>

#include <cstddef>
#include <execution>
#include <memory>
#include <memory_resource>
#include <new>
#include <utility>
#include <vector>

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L
#include <coroutine>

#if defined(_MSC_VER)
#define FORGE_TEST_NOINLINE __declspec(noinline)
#else
#define FORGE_TEST_NOINLINE __attribute__((noinline))
#endif

namespace {

namespace cio = forge::io;

// Records every allocation with its exact size and alignment and checks that
// each deallocation matches one live allocation bit for bit. This is the
// oracle proving coroutine frames really travel through the given resource.
class recording_resource final : public std::pmr::memory_resource {
public:
    [[nodiscard]] auto allocate_calls() const noexcept -> std::size_t {
        return allocate_calls_;
    }

    [[nodiscard]] auto deallocate_calls() const noexcept -> std::size_t {
        return deallocate_calls_;
    }

    [[nodiscard]] auto live() const noexcept -> std::size_t {
        return live_.size();
    }

    [[nodiscard]] auto mismatched() const noexcept -> bool {
        return mismatched_;
    }

private:
    struct entry {
        void* pointer;
        std::size_t size;
        std::size_t alignment;
    };

    auto do_allocate(std::size_t size, std::size_t alignment)
        -> void* override {
        void* pointer =
            std::pmr::new_delete_resource()->allocate(size, alignment);
        live_.push_back(entry{pointer, size, alignment});
        ++allocate_calls_;
        return pointer;
    }

    auto do_deallocate(
        void* pointer,
        std::size_t size,
        std::size_t alignment) noexcept -> void override {
        bool found = false;
        for (std::size_t index = 0; index < live_.size(); ++index) {
            const entry& item = live_[index];
            if (item.pointer == pointer && item.size == size &&
                item.alignment == alignment) {
                live_.erase(live_.begin() + static_cast<std::ptrdiff_t>(index));
                found = true;
                break;
            }
        }
        if (!found) {
            mismatched_ = true;
        }
        ++deallocate_calls_;
        std::pmr::new_delete_resource()->deallocate(pointer, size, alignment);
    }

    [[nodiscard]] auto do_is_equal(
        const std::pmr::memory_resource& other) const noexcept
        -> bool override {
        return this == &other;
    }

    std::vector<entry> live_{};
    std::size_t allocate_calls_ = 0;
    std::size_t deallocate_calls_ = 0;
    bool mismatched_ = false;
};

class throwing_resource final : public std::pmr::memory_resource {
private:
    auto do_allocate(std::size_t, std::size_t) -> void* override {
        throw std::bad_alloc{};
    }

    auto do_deallocate(void*, std::size_t, std::size_t) noexcept
        -> void override {}

    [[nodiscard]] auto do_is_equal(
        const std::pmr::memory_resource& other) const noexcept
        -> bool override {
        return this == &other;
    }
};

auto counted_value(std::allocator_arg_t, std::pmr::memory_resource*)
    -> cio::io_task<int> {
    co_return 41;
}

auto counted_void(std::allocator_arg_t, std::pmr::memory_resource*, bool* ran)
    -> cio::io_task<void> {
    *ran = true;
    co_return;
}

auto child_sum(std::allocator_arg_t, std::pmr::memory_resource*, int a, int b)
    -> cio::io_task<int> {
    co_return a + b;
}

// The taskbook pattern: the parent frame comes from the caller-provided
// resource and the child frame is created by explicitly re-passing
// io_env::memory. There is no ambient propagation.
auto parent_sum(std::allocator_arg_t, std::pmr::memory_resource*, int a, int b)
    -> cio::io_task<int> {
    const auto& env = co_await cio::this_io_env();
    co_return co_await child_sum(std::allocator_arg, env.memory, a, b);
}

auto plain_value() -> cio::io_task<int> {
    const auto& env = co_await cio::this_io_env();
    co_return env.memory != nullptr ? 17 : -1;
}

// The factories are noinline so the returned task provably escapes the
// coroutine ramp caller; heap-allocation elision (HALO) therefore cannot
// remove the outermost frame allocation at any optimization level.
FORGE_TEST_NOINLINE auto make_counted_value(std::pmr::memory_resource* memory)
    -> cio::io_task<int> {
    return counted_value(std::allocator_arg, memory);
}

FORGE_TEST_NOINLINE auto make_counted_void(
    std::pmr::memory_resource* memory,
    bool* ran) -> cio::io_task<void> {
    return counted_void(std::allocator_arg, memory, ran);
}

FORGE_TEST_NOINLINE auto make_parent_sum(
    std::pmr::memory_resource* memory,
    int a,
    int b) -> cio::io_task<int> {
    return parent_sum(std::allocator_arg, memory, a, b);
}

// Member and lambda coroutines receive their implicit object parameter in
// front of the declared parameters on some compilers ([dcl.fct.def.
// coroutine]/9); the This-aware operator new overload keeps the explicit
// resource protocol working identically across compilers.
struct member_coroutine_host {
    int bias = 1;

    auto counted(
        std::allocator_arg_t,
        std::pmr::memory_resource*,
        int value) -> cio::io_task<int> {
        co_return value + bias;
    }
};

FORGE_TEST_NOINLINE auto make_member_counted(
    member_coroutine_host& host,
    std::pmr::memory_resource* memory,
    int value) -> cio::io_task<int> {
    return host.counted(std::allocator_arg, memory, value);
}

FORGE_TEST_NOINLINE auto make_lambda_counted(
    std::pmr::memory_resource* memory,
    int value) -> cio::io_task<int> {
    auto lambda = [](
        std::allocator_arg_t,
        std::pmr::memory_resource*,
        int inner) -> cio::io_task<int> {
        co_return inner * 2;
    };
    return lambda(std::allocator_arg, memory, value);
}

} // namespace

TEST(ForgeIoTaskAllocTest, ExplicitResourceOwnsTheFrame) {
    recording_resource memory;

    {
        auto task = make_counted_value(&memory);
        EXPECT_EQ(memory.allocate_calls(), 1U);
        EXPECT_EQ(memory.live(), 1U);

        auto result = std::execution::sync_wait(cio::as_sender(std::move(task)));
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(std::get<0>(*result), 41);
    }

    EXPECT_EQ(memory.allocate_calls(), 1U);
    EXPECT_EQ(memory.deallocate_calls(), memory.allocate_calls());
    EXPECT_EQ(memory.live(), 0U);
    EXPECT_FALSE(memory.mismatched());
}

TEST(ForgeIoTaskAllocTest, VoidTaskFrameUsesTheResourceToo) {
    recording_resource memory;
    bool ran = false;

    {
        auto task = make_counted_void(&memory, &ran);
        EXPECT_EQ(memory.allocate_calls(), 1U);

        auto result = std::execution::sync_wait(cio::as_sender(std::move(task)));
        ASSERT_TRUE(result.has_value());
    }

    EXPECT_TRUE(ran);
    EXPECT_EQ(memory.deallocate_calls(), memory.allocate_calls());
    EXPECT_EQ(memory.live(), 0U);
    EXPECT_FALSE(memory.mismatched());
}

TEST(ForgeIoTaskAllocTest, NestedExplicitRePassHitsTheSameResource) {
    recording_resource memory;

    cio::io_env env;
    env.memory = &memory;

    {
        auto task = make_parent_sum(&memory, 19, 23);
        EXPECT_EQ(memory.allocate_calls(), 1U);

        auto result = std::execution::sync_wait(
            cio::as_sender(std::move(task), env));
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(std::get<0>(*result), 42);
    }

    // The child frame is created inside the parent coroutine, so HALO may
    // legally elide it at higher optimization levels; seeing one or two
    // allocations are both correct outcomes.
    EXPECT_GE(memory.allocate_calls(), 1U);
    EXPECT_LE(memory.allocate_calls(), 2U);
    EXPECT_EQ(memory.deallocate_calls(), memory.allocate_calls());
    EXPECT_EQ(memory.live(), 0U);
    EXPECT_FALSE(memory.mismatched());
}

TEST(ForgeIoTaskAllocTest, EnvMemoryAloneDoesNotAllocateFrames) {
    recording_resource memory;

    cio::io_env env;
    env.memory = &memory;

    auto result = std::execution::sync_wait(
        cio::as_sender(plain_value(), env));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 17);
    EXPECT_EQ(memory.allocate_calls(), 0U);
    EXPECT_EQ(memory.deallocate_calls(), 0U);
}

TEST(ForgeIoTaskAllocTest, MemberCoroutineExplicitResourceOwnsTheFrame) {
    recording_resource memory;
    member_coroutine_host host;

    {
        auto task = make_member_counted(host, &memory, 41);
        EXPECT_EQ(memory.allocate_calls(), 1U);

        auto result = std::execution::sync_wait(cio::as_sender(std::move(task)));
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(std::get<0>(*result), 42);
    }

    EXPECT_EQ(memory.deallocate_calls(), memory.allocate_calls());
    EXPECT_EQ(memory.live(), 0U);
    EXPECT_FALSE(memory.mismatched());
}

TEST(ForgeIoTaskAllocTest, LambdaCoroutineExplicitResourceOwnsTheFrame) {
    recording_resource memory;

    {
        auto task = make_lambda_counted(&memory, 21);
        EXPECT_EQ(memory.allocate_calls(), 1U);

        auto result = std::execution::sync_wait(cio::as_sender(std::move(task)));
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(std::get<0>(*result), 42);
    }

    EXPECT_EQ(memory.deallocate_calls(), memory.allocate_calls());
    EXPECT_EQ(memory.live(), 0U);
    EXPECT_FALSE(memory.mismatched());
}

TEST(ForgeIoTaskAllocTest, NullResourceFallsBackToTheGlobalPath) {
    auto result = std::execution::sync_wait(cio::as_sender(
        counted_value(std::allocator_arg, nullptr)));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 41);
}

TEST(ForgeIoTaskAllocTest, AllocationFailurePropagatesAndLeaksNothing) {
    throwing_resource broken;

    EXPECT_THROW((void)make_counted_value(&broken), std::bad_alloc);

    recording_resource memory;
    auto result = std::execution::sync_wait(cio::as_sender(
        make_counted_value(&memory)));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 41);
    EXPECT_EQ(memory.deallocate_calls(), memory.allocate_calls());
    EXPECT_FALSE(memory.mismatched());
}

#else

TEST(ForgeIoTaskAllocTest, CoroutinesNotAvailable) {
    GTEST_SKIP() << "C++20 coroutines not available on this toolchain";
}

#endif
