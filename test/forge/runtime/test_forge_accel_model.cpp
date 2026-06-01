#include <gtest/gtest.h>
#include <forge/accel.hpp>
#include <forge/erased_sender.hpp>
#include <execution>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <vector>

namespace {

using namespace std::chrono_literals;

[[nodiscard]] auto make_descriptor() -> forge::accel::mock::model_descriptor {
    return forge::accel::mock::model_descriptor{
        .inputs = {
            forge::accel::model_io_descriptor{
                .byte_size = 4,
                .rank = 1,
                .extents = {4, 0, 0, 0},
            },
        },
        .outputs = {
            forge::accel::model_io_descriptor{
                .byte_size = 4,
                .rank = 1,
                .extents = {4, 0, 0, 0},
            },
        },
    };
}

struct typed_state {
    std::mutex mtx;
    std::condition_variable cv;
    bool value = false;
    bool stopped = false;
    bool error_seen = false;
    forge::accel::error error{};

    [[nodiscard]] bool done() const noexcept {
        return value || stopped || error_seen;
    }
};

struct typed_receiver {
    using receiver_concept = std::execution::receiver_t;

    std::shared_ptr<typed_state> state;

    void set_value() && noexcept {
        {
            std::lock_guard lk{state->mtx};
            state->value = true;
        }
        state->cv.notify_all();
    }

    void set_error(forge::accel::error error) && noexcept {
        {
            std::lock_guard lk{state->mtx};
            state->error_seen = true;
            state->error = std::move(error);
        }
        state->cv.notify_all();
    }

    void set_stopped() && noexcept {
        {
            std::lock_guard lk{state->mtx};
            state->stopped = true;
        }
        state->cv.notify_all();
    }

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }
};

using model_error_cs = std::execution::completion_signatures<
    std::execution::set_value_t(),
    std::execution::set_error_t(forge::accel::error),
    std::execution::set_stopped_t()>;

} // namespace

TEST(AccelModelTest, ModelReportsMetadataAndUnloadState) {
    forge::accel::mock::model model{make_descriptor()};

    auto info = model.info();
    EXPECT_EQ(info.inputs, 1u);
    EXPECT_EQ(info.outputs, 1u);
    EXPECT_EQ(model.input(0).byte_size, 4u);
    EXPECT_EQ(model.output(0).extents[0], 4u);
    EXPECT_TRUE(model.loaded());

    model.unload();
    EXPECT_FALSE(model.loaded());
}

TEST(AccelModelTest, ExecuteWritesDeterministicOutput) {
    forge::accel::mock::context ctx;
    forge::accel::mock::model model{make_descriptor()};
    auto session = model.open_session(ctx.get_device());
    forge::accel::mock::model_bindings bindings{model};
    std::vector<std::byte> input{
        std::byte{1},
        std::byte{2},
        std::byte{3},
        std::byte{4},
    };
    std::vector<std::byte> output(4);
    bindings.bind_input(0, std::span<const std::byte>{input});
    bindings.bind_output(0, std::span<std::byte>{output});

    auto result = std::execution::sync_wait(
        forge::accel::mock::execute(session, std::move(bindings)));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(output[0], std::byte{10});
    EXPECT_EQ(output[1], std::byte{11});
    EXPECT_EQ(output[2], std::byte{12});
    EXPECT_EQ(output[3], std::byte{13});
}

TEST(AccelModelTest, MissingBindingIsRejected) {
    forge::accel::mock::context ctx;
    forge::accel::mock::model model{make_descriptor()};
    auto session = model.open_session(ctx.get_device());
    forge::accel::mock::model_bindings bindings{model};
    std::vector<std::byte> input(4);
    bindings.bind_input(0, std::span<const std::byte>{input});

    EXPECT_THROW(
        (void)std::execution::sync_wait(
            forge::accel::mock::execute(session, std::move(bindings))),
        forge::accel::operation_error);
}

TEST(AccelModelTest, SizeMismatchIsRejected) {
    forge::accel::mock::context ctx;
    forge::accel::mock::model model{make_descriptor()};
    auto session = model.open_session(ctx.get_device());
    forge::accel::mock::model_bindings bindings{model};
    std::vector<std::byte> input(3);
    std::vector<std::byte> output(4);
    bindings.bind_input(0, std::span<const std::byte>{input});
    bindings.bind_output(0, std::span<std::byte>{output});

    EXPECT_THROW(
        (void)std::execution::sync_wait(
            forge::accel::mock::execute(session, std::move(bindings))),
        forge::accel::operation_error);
}

TEST(AccelModelTest, ResetStopsExecuteBeforeStart) {
    forge::accel::mock::context ctx;
    forge::accel::mock::model model{make_descriptor()};
    auto session = model.open_session(ctx.get_device());
    forge::accel::mock::model_bindings bindings{model};
    std::vector<std::byte> input(4);
    std::vector<std::byte> output(4);
    bindings.bind_input(0, std::span<const std::byte>{input});
    bindings.bind_output(0, std::span<std::byte>{output});

    session.reset();
    auto result = std::execution::sync_wait(
        forge::accel::mock::execute(session, std::move(bindings)));

    EXPECT_FALSE(result.has_value());
}

TEST(AccelModelTest, ContextShutdownStopsExecute) {
    forge::accel::mock::context ctx;
    forge::accel::mock::model model{make_descriptor()};
    auto session = model.open_session(ctx.get_device());
    forge::accel::mock::model_bindings bindings{model};
    std::vector<std::byte> input(4);
    std::vector<std::byte> output(4);
    bindings.bind_input(0, std::span<const std::byte>{input});
    bindings.bind_output(0, std::span<std::byte>{output});

    ctx.shutdown();
    auto result = std::execution::sync_wait(
        forge::accel::mock::execute(session, std::move(bindings)));

    EXPECT_FALSE(result.has_value());
}

TEST(AccelModelTest, TypedExecuteErrorCrossesErasedSenderBoundary) {
    forge::accel::mock::context ctx;
    forge::accel::mock::model model{make_descriptor()};
    auto session = model.open_session(ctx.get_device());
    forge::accel::mock::model_bindings bindings{model};
    std::vector<std::byte> input(4);
    bindings.bind_input(0, std::span<const std::byte>{input});
    auto state = std::make_shared<typed_state>();

    forge::erased_sender<model_error_cs> erased{
        forge::accel::mock::execute_typed(session, std::move(bindings))};
    auto op = std::execution::connect(std::move(erased), typed_receiver{state});
    std::execution::start(op);

    {
        std::unique_lock lk{state->mtx};
        ASSERT_TRUE(state->cv.wait_for(lk, 2s, [&] { return state->done(); }));
    }
    EXPECT_FALSE(state->value);
    EXPECT_FALSE(state->stopped);
    ASSERT_TRUE(state->error_seen);
    EXPECT_EQ(state->error.kind, forge::accel::error_kind::invalid_binding);
    EXPECT_TRUE(state->error.cause);
}
