#include <gtest/gtest.h>
#include <forge/accel.hpp>
#include "forge_counting_resource.hpp"
#include <execution>
#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <vector>

TEST(AccelCopyTest, HostDeviceRoundTrip) {
    forge::accel::mock::context ctx;
    auto q = ctx.get_queue();
    forge::accel::mock::device_buffer<int> device{ctx, 4};
    std::vector<int> input{1, 2, 3, 4};
    std::vector<int> output(4);

    ASSERT_TRUE(std::execution::sync_wait(
        forge::accel::mock::copy_to_device(q, device, std::span<const int>{input})).has_value());
    ASSERT_TRUE(std::execution::sync_wait(
        forge::accel::mock::copy_to_host(q, std::span<int>{output}, device)).has_value());

    EXPECT_EQ(output, input);
}

TEST(AccelCopyTest, HostBufferUsesResourceAndCopiesThroughDevice) {
    forge_test::counting_resource resource;
    std::vector<int> output(4);

    {
        forge::accel::mock::context ctx{forge::accel::mock::context_options{
            .memory = &resource,
        }};
        auto q = ctx.get_queue();
        forge::accel::mock::host_buffer<int> input{ctx, 4};
        forge::accel::mock::host_buffer<int> staging{ctx, 4};
        forge::accel::mock::device_buffer<int> device{ctx, 4};

        input.span()[0] = 2;
        input.span()[1] = 4;
        input.span()[2] = 6;
        input.span()[3] = 8;

        ASSERT_TRUE(std::execution::sync_wait(
            forge::accel::mock::copy_to_device(q, device, input.span())).has_value());
        ASSERT_TRUE(std::execution::sync_wait(
            forge::accel::mock::copy_to_host(q, staging.span(), device)).has_value());

        std::ranges::copy(staging.span(), output.begin());
        EXPECT_GT(resource.allocations(), 0u);
    }

    EXPECT_EQ(output, (std::vector<int>{2, 4, 6, 8}));
    EXPECT_EQ(resource.allocations(), resource.deallocations());
}

TEST(AccelCopyTest, MemoryKindsAndByteBuffersAreVisible) {
    forge::accel::mock::context ctx;
    auto q = ctx.get_queue();
    forge::accel::mock::host_buffer<std::byte> input{
        ctx,
        3,
        forge::accel::memory_kind::pinned_host};
    forge::accel::mock::device_byte_buffer device{
        ctx,
        3,
        forge::accel::memory_kind::device};
    forge::accel::mock::host_byte_buffer output{
        ctx,
        3,
        forge::accel::memory_kind::mapped_host};

    input.span()[0] = std::byte{0x11};
    input.span()[1] = std::byte{0x22};
    input.span()[2] = std::byte{0x33};

    EXPECT_EQ(input.kind(), forge::accel::memory_kind::pinned_host);
    EXPECT_EQ(device.kind(), forge::accel::memory_kind::device);
    EXPECT_EQ(output.kind(), forge::accel::memory_kind::mapped_host);

    ASSERT_TRUE(std::execution::sync_wait(
        forge::accel::mock::copy_to_device(q, device, input)).has_value());
    ASSERT_TRUE(std::execution::sync_wait(
        forge::accel::mock::copy_to_host(q, output, device)).has_value());

    EXPECT_TRUE(std::ranges::equal(input.span(), output.span()));
}

TEST(AccelCopyTest, RejectsInvalidMemoryKinds) {
    forge::accel::mock::context ctx;

    EXPECT_THROW(
        (forge::accel::mock::host_buffer<int>{
            ctx,
            4,
            forge::accel::memory_kind::device}),
        forge::accel::operation_error);
    EXPECT_THROW(
        (forge::accel::mock::device_buffer<int>{
            ctx,
            4,
            forge::accel::memory_kind::pinned_host}),
        forge::accel::operation_error);
}

TEST(AccelCopyTest, CachedDeviceRequiresFlushBeforeReadback) {
    forge::accel::mock::context ctx;
    auto q = ctx.get_queue();
    forge::accel::mock::device_buffer<int> device{
        ctx,
        3,
        forge::accel::memory_kind::cached_device};
    std::vector<int> input{1, 4, 9};
    std::vector<int> output(3);

    ASSERT_TRUE(std::execution::sync_wait(
        forge::accel::mock::copy_to_device(q, device, std::span<const int>{input})).has_value());
    EXPECT_TRUE(device.needs_flush());

    EXPECT_THROW(
        (void)std::execution::sync_wait(
            forge::accel::mock::copy_to_host(q, std::span<int>{output}, device)),
        forge::accel::operation_error);

    ASSERT_TRUE(std::execution::sync_wait(
        forge::accel::mock::flush(q, device)).has_value());
    EXPECT_FALSE(device.needs_flush());
    ASSERT_TRUE(std::execution::sync_wait(
        forge::accel::mock::copy_to_host(q, std::span<int>{output}, device)).has_value());

    EXPECT_EQ(output, input);
}

TEST(AccelCopyTest, CachedDeviceRequiresInvalidateAfterDeviceWrite) {
    forge::accel::mock::context ctx;
    auto q = ctx.get_queue();
    forge::accel::mock::device_buffer<int> source{ctx, 3};
    forge::accel::mock::device_buffer<int> cached{
        ctx,
        3,
        forge::accel::memory_kind::cached_device};
    std::vector<int> input{2, 3, 5};
    std::vector<int> output(3);

    ASSERT_TRUE(std::execution::sync_wait(
        forge::accel::mock::copy_to_device(q, source, std::span<const int>{input})).has_value());
    ASSERT_TRUE(std::execution::sync_wait(
        forge::accel::mock::copy_device_to_device(q, cached, source)).has_value());
    EXPECT_TRUE(cached.needs_invalidate());

    EXPECT_THROW(
        (void)std::execution::sync_wait(
            forge::accel::mock::copy_to_host(q, std::span<int>{output}, cached)),
        forge::accel::operation_error);

    ASSERT_TRUE(std::execution::sync_wait(
        forge::accel::mock::invalidate(q, cached)).has_value());
    EXPECT_FALSE(cached.needs_invalidate());
    ASSERT_TRUE(std::execution::sync_wait(
        forge::accel::mock::copy_to_host(q, std::span<int>{output}, cached)).has_value());

    EXPECT_EQ(output, input);
}

TEST(AccelCopyTest, DeviceToDeviceCopy) {
    forge::accel::mock::context ctx;
    auto q = ctx.get_queue();
    forge::accel::mock::device_buffer<int> first{ctx, 3};
    forge::accel::mock::device_buffer<int> second{ctx, 3};
    std::vector<int> input{5, 8, 13};
    std::vector<int> output(3);

    ASSERT_TRUE(std::execution::sync_wait(
        forge::accel::mock::copy_to_device(q, first, std::span<const int>{input})).has_value());
    ASSERT_TRUE(std::execution::sync_wait(
        forge::accel::mock::copy_device_to_device(q, second, first)).has_value());
    ASSERT_TRUE(std::execution::sync_wait(
        forge::accel::mock::copy_to_host(q, std::span<int>{output}, second)).has_value());

    EXPECT_EQ(output, input);
}

TEST(AccelCopyTest, SizeMismatchRoutesError) {
    forge::accel::mock::context ctx;
    auto q = ctx.get_queue();
    forge::accel::mock::device_buffer<int> device{ctx, 2};
    std::vector<int> input{1, 2, 3};

    EXPECT_THROW(
        (void)std::execution::sync_wait(
            forge::accel::mock::copy_to_device(q, device, std::span<const int>{input})),
        std::runtime_error);
}

TEST(AccelCopyTest, SubmitCanTransformDeviceBuffer) {
    forge::accel::mock::context ctx;
    auto q = ctx.get_queue();
    forge::accel::mock::device_buffer<int> device{ctx, 4};
    std::vector<int> input{1, 2, 3, 4};
    std::vector<int> output(4);

    ASSERT_TRUE(std::execution::sync_wait(
        forge::accel::mock::copy_to_device(q, device, std::span<const int>{input})).has_value());
    ASSERT_TRUE(std::execution::sync_wait(
        forge::accel::mock::submit(q, [&] {
            for (auto& value : device.span()) {
                value *= 3;
            }
        })).has_value());
    ASSERT_TRUE(std::execution::sync_wait(
        forge::accel::mock::copy_to_host(q, std::span<int>{output}, device)).has_value());

    EXPECT_EQ(output, (std::vector<int>{3, 6, 9, 12}));
}
