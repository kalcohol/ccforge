#include <gtest/gtest.h>
#include <forge/accel.hpp>
#include <execution>
#include <stdexcept>
#include <vector>

namespace {

struct stop_env {
    std::inplace_stop_source* source;

    friend auto tag_invoke(
        std::execution::get_stop_token_t,
        const stop_env& self) noexcept -> std::inplace_stop_token {
        return self.source->get_token();
    }
};

struct stopped_receiver {
    using receiver_concept = std::execution::receiver_t;

    std::inplace_stop_source* source;
    bool* value;
    bool* stopped;

    void set_value() && noexcept { *value = true; }
    void set_error(std::exception_ptr) && noexcept { *value = true; }
    void set_stopped() && noexcept { *stopped = true; }

    auto get_env() const noexcept -> stop_env {
        return stop_env{source};
    }
};

} // namespace

TEST(AccelSubmitTest, SubmitMutatesDeviceBuffer) {
    forge::accel::context ctx;
    auto q = ctx.get_queue();
    forge::accel::device_buffer<int> device{ctx, 3};
    std::vector<int> input{2, 4, 6};
    std::vector<int> output(3);

    ASSERT_TRUE(std::execution::sync_wait(
        forge::accel::copy_to_device(q, device, std::span<const int>{input})).has_value());
    ASSERT_TRUE(std::execution::sync_wait(
        forge::accel::submit(q, [&] {
            auto values = device.span();
            values[0] += 10;
            values[1] += 20;
            values[2] += 30;
        })).has_value());
    ASSERT_TRUE(std::execution::sync_wait(
        forge::accel::copy_to_host(q, std::span<int>{output}, device)).has_value());

    EXPECT_EQ(output, (std::vector<int>{12, 24, 36}));
}

TEST(AccelSubmitTest, SubmitExceptionsRouteError) {
    forge::accel::context ctx;
    auto q = ctx.get_queue();

    EXPECT_THROW(
        (void)std::execution::sync_wait(
            forge::accel::submit(q, [] {
                throw std::runtime_error{"kernel failed"};
            })),
        std::runtime_error);
}

TEST(AccelSubmitTest, PreStoppedReceiverCompletesStoppedWithoutRunningCommand) {
    forge::accel::context ctx;
    auto q = ctx.get_queue();
    std::inplace_stop_source source;
    source.request_stop();

    bool ran = false;
    bool value = false;
    bool stopped = false;
    auto sender = forge::accel::submit(q, [&] {
        ran = true;
    });
    auto op = std::execution::connect(
        std::move(sender),
        stopped_receiver{&source, &value, &stopped});

    std::execution::start(op);
    ctx.wait();

    EXPECT_FALSE(ran);
    EXPECT_FALSE(value);
    EXPECT_TRUE(stopped);
}

TEST(AccelSubmitTest, CommandsComposeWithThen) {
    forge::accel::context ctx;
    auto q = ctx.get_queue();
    int value = 0;

    auto result = std::execution::sync_wait(
        forge::accel::submit(q, [&] {
            value = 41;
        }) | std::execution::then([&] {
            return value + 1;
        }));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 42);
}
