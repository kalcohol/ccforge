#include <gtest/gtest.h>
#include <forge/accel.hpp>
#include <execution>
#include <stdexcept>
#include <vector>

TEST(AccelCopyTest, HostDeviceRoundTrip) {
    forge::accel::context ctx;
    auto q = ctx.get_queue();
    forge::accel::device_buffer<int> device{ctx, 4};
    std::vector<int> input{1, 2, 3, 4};
    std::vector<int> output(4);

    ASSERT_TRUE(std::execution::sync_wait(
        forge::accel::copy_to_device(q, device, std::span<const int>{input})).has_value());
    ASSERT_TRUE(std::execution::sync_wait(
        forge::accel::copy_to_host(q, std::span<int>{output}, device)).has_value());

    EXPECT_EQ(output, input);
}

TEST(AccelCopyTest, DeviceToDeviceCopy) {
    forge::accel::context ctx;
    auto q = ctx.get_queue();
    forge::accel::device_buffer<int> first{ctx, 3};
    forge::accel::device_buffer<int> second{ctx, 3};
    std::vector<int> input{5, 8, 13};
    std::vector<int> output(3);

    ASSERT_TRUE(std::execution::sync_wait(
        forge::accel::copy_to_device(q, first, std::span<const int>{input})).has_value());
    ASSERT_TRUE(std::execution::sync_wait(
        forge::accel::copy_device_to_device(q, second, first)).has_value());
    ASSERT_TRUE(std::execution::sync_wait(
        forge::accel::copy_to_host(q, std::span<int>{output}, second)).has_value());

    EXPECT_EQ(output, input);
}

TEST(AccelCopyTest, SizeMismatchRoutesError) {
    forge::accel::context ctx;
    auto q = ctx.get_queue();
    forge::accel::device_buffer<int> device{ctx, 2};
    std::vector<int> input{1, 2, 3};

    EXPECT_THROW(
        (void)std::execution::sync_wait(
            forge::accel::copy_to_device(q, device, std::span<const int>{input})),
        std::runtime_error);
}

TEST(AccelCopyTest, SubmitCanTransformDeviceBuffer) {
    forge::accel::context ctx;
    auto q = ctx.get_queue();
    forge::accel::device_buffer<int> device{ctx, 4};
    std::vector<int> input{1, 2, 3, 4};
    std::vector<int> output(4);

    ASSERT_TRUE(std::execution::sync_wait(
        forge::accel::copy_to_device(q, device, std::span<const int>{input})).has_value());
    ASSERT_TRUE(std::execution::sync_wait(
        forge::accel::submit(q, [&] {
            for (auto& value : device.span()) {
                value *= 3;
            }
        })).has_value());
    ASSERT_TRUE(std::execution::sync_wait(
        forge::accel::copy_to_host(q, std::span<int>{output}, device)).has_value());

    EXPECT_EQ(output, (std::vector<int>{3, 6, 9, 12}));
}
