#include <gtest/gtest.h>
#include <forge/accel.hpp>
#include "forge_counting_resource.hpp"
#include <execution>
#include <algorithm>
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
