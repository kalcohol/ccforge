#include <gtest/gtest.h>
#include <forge/io.hpp>
#include <execution>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <system_error>
#include <tuple>
#include <utility>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace {

using namespace std::chrono_literals;

[[nodiscard]] auto byte(char value) noexcept -> std::byte {
    return std::byte{static_cast<unsigned char>(value)};
}

[[noreturn]] void throw_last_error(const char* what) {
    throw std::system_error{
        static_cast<int>(::GetLastError()),
        std::system_category(),
        what};
}

class unique_handle {
public:
    unique_handle() noexcept = default;
    explicit unique_handle(HANDLE handle) noexcept : handle_(handle) {}
    ~unique_handle() noexcept { reset(); }

    unique_handle(unique_handle&& other) noexcept
        : handle_(std::exchange(other.handle_, INVALID_HANDLE_VALUE)) {}
    auto operator=(unique_handle&& other) noexcept -> unique_handle& {
        if (this != &other) {
            reset(std::exchange(other.handle_, INVALID_HANDLE_VALUE));
        }
        return *this;
    }

    unique_handle(const unique_handle&) = delete;
    auto operator=(const unique_handle&) -> unique_handle& = delete;

    [[nodiscard]] auto get() const noexcept -> HANDLE { return handle_; }
    [[nodiscard]] explicit operator bool() const noexcept {
        return handle_ && handle_ != INVALID_HANDLE_VALUE;
    }

    void reset(HANDLE next = INVALID_HANDLE_VALUE) noexcept {
        if (handle_ && handle_ != INVALID_HANDLE_VALUE) {
            ::CloseHandle(handle_);
        }
        handle_ = next;
    }

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
};

struct pipe_pair {
    unique_handle server;
    unique_handle client;
};

[[nodiscard]] auto make_pipe_pair() -> pipe_pair {
    auto name = std::wstring{LR"(\\.\pipe\ccforge-iocp-)"} +
        std::to_wstring(::GetCurrentProcessId()) + L"-" +
        std::to_wstring(::GetTickCount64());

    unique_handle server{::CreateNamedPipeW(
        name.c_str(),
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        1,
        4096,
        4096,
        0,
        nullptr)};
    if (!server) {
        throw_last_error("CreateNamedPipeW");
    }

    unique_handle client{::CreateFileW(
        name.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
        nullptr)};
    if (!client) {
        throw_last_error("CreateFileW pipe client");
    }

    if (!::ConnectNamedPipe(server.get(), nullptr) &&
        ::GetLastError() != ERROR_PIPE_CONNECTED) {
        throw_last_error("ConnectNamedPipe");
    }

    return pipe_pair{std::move(server), std::move(client)};
}

struct io_state {
    std::mutex mtx;
    std::condition_variable cv;
    bool value = false;
    bool stopped = false;
    std::exception_ptr error;
    std::size_t bytes = 0;

    [[nodiscard]] bool done() const noexcept {
        return value || stopped || error;
    }
};

struct io_receiver {
    using receiver_concept = std::execution::receiver_t;

    std::shared_ptr<io_state> state;

    void set_value(std::size_t bytes) && noexcept {
        {
            std::lock_guard lk{state->mtx};
            state->value = true;
            state->bytes = bytes;
        }
        state->cv.notify_all();
    }

    void set_error(std::exception_ptr error) && noexcept {
        {
            std::lock_guard lk{state->mtx};
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

struct stop_env {
    std::inplace_stop_source* source;

    friend auto tag_invoke(
        std::execution::get_stop_token_t,
        const stop_env& self) noexcept -> std::inplace_stop_token {
        return self.source->get_token();
    }
};

struct stopped_receiver : io_receiver {
    std::inplace_stop_source* source;

    auto get_env() const noexcept -> stop_env {
        return stop_env{source};
    }
};

[[nodiscard]] auto wait_done(const std::shared_ptr<io_state>& state) -> bool {
    std::unique_lock lk{state->mtx};
    return state->cv.wait_for(lk, 2s, [&] { return state->done(); });
}

} // namespace

TEST(IoIocpTest, EmptyContextDestroysCleanly) {
    forge::io::context ctx;
    ctx.shutdown();
    ctx.wait();
}

TEST(IoIocpTest, AsyncWriteAndReadNamedPipe) {
    auto pipe = make_pipe_pair();
    forge::io::context ctx;

    std::array<std::byte, 3> payload{byte('i'), byte('o'), byte('c')};
    auto write_result = std::execution::sync_wait(
        ctx.async_write_some(pipe.client.get(), std::span<const std::byte>{payload}));

    ASSERT_TRUE(write_result.has_value());
    EXPECT_EQ(std::get<0>(*write_result), payload.size());

    std::array<std::byte, 3> buffer{};
    auto read_result = std::execution::sync_wait(
        ctx.async_read_some(pipe.server.get(), std::span{buffer}));

    ASSERT_TRUE(read_result.has_value());
    EXPECT_EQ(std::get<0>(*read_result), payload.size());
    EXPECT_EQ(buffer, payload);
}

TEST(IoIocpTest, ReusesAssociatedHandleForSequentialOperations) {
    auto pipe = make_pipe_pair();
    forge::io::context ctx;

    std::array<std::byte, 2> first{byte('o'), byte('n')};
    auto first_write = std::execution::sync_wait(
        ctx.async_write_some(pipe.client.get(), std::span<const std::byte>{first}));

    ASSERT_TRUE(first_write.has_value());
    EXPECT_EQ(std::get<0>(*first_write), first.size());

    std::array<std::byte, 2> first_read{};
    auto first_read_result = std::execution::sync_wait(
        ctx.async_read_some(pipe.server.get(), std::span{first_read}));

    ASSERT_TRUE(first_read_result.has_value());
    EXPECT_EQ(first_read, first);

    std::array<std::byte, 3> second{byte('t'), byte('w'), byte('o')};
    auto second_write = std::execution::sync_wait(
        ctx.async_write_some(pipe.client.get(), std::span<const std::byte>{second}));

    ASSERT_TRUE(second_write.has_value());
    EXPECT_EQ(std::get<0>(*second_write), second.size());

    std::array<std::byte, 3> second_read{};
    auto second_read_result = std::execution::sync_wait(
        ctx.async_read_some(pipe.server.get(), std::span{second_read}));

    ASSERT_TRUE(second_read_result.has_value());
    EXPECT_EQ(second_read, second);
}

TEST(IoIocpTest, RequestStopCancelsPendingRead) {
    auto pipe = make_pipe_pair();
    forge::io::context ctx;
    std::array<std::byte, 8> buffer{};
    auto state = std::make_shared<io_state>();

    auto op = std::execution::connect(
        ctx.async_read_some(pipe.server.get(), std::span{buffer}),
        io_receiver{state});
    std::execution::start(op);

    ctx.request_stop();

    ASSERT_TRUE(wait_done(state));
    EXPECT_FALSE(state->value);
    EXPECT_TRUE(state->stopped);
    EXPECT_FALSE(state->error);
}

TEST(IoIocpTest, PreStartReceiverStopCompletesStopped) {
    auto pipe = make_pipe_pair();
    forge::io::context ctx;
    std::array<std::byte, 8> buffer{};
    auto state = std::make_shared<io_state>();
    std::inplace_stop_source source;
    source.request_stop();

    auto op = std::execution::connect(
        ctx.async_read_some(pipe.server.get(), std::span{buffer}),
        stopped_receiver{{state}, &source});
    std::execution::start(op);

    ASSERT_TRUE(wait_done(state));
    EXPECT_FALSE(state->value);
    EXPECT_TRUE(state->stopped);
    EXPECT_FALSE(state->error);
}

TEST(IoIocpTest, ImmediateReceiverStopAfterStartCancelsPendingRead) {
    auto pipe = make_pipe_pair();
    forge::io::context ctx;
    std::array<std::byte, 8> buffer{};
    auto state = std::make_shared<io_state>();
    std::inplace_stop_source source;

    auto op = std::execution::connect(
        ctx.async_read_some(pipe.server.get(), std::span{buffer}),
        stopped_receiver{{state}, &source});
    std::execution::start(op);
    source.request_stop();

    ASSERT_TRUE(wait_done(state));
    EXPECT_FALSE(state->value);
    EXPECT_TRUE(state->stopped);
    EXPECT_FALSE(state->error);
}

TEST(IoIocpTest, PostEnqueueReceiverStopCancelsPendingRead) {
    auto pipe = make_pipe_pair();
    forge::io::context ctx;
    std::array<std::byte, 8> buffer{};
    auto state = std::make_shared<io_state>();
    std::inplace_stop_source source;

    auto op = std::execution::connect(
        ctx.async_read_some(pipe.server.get(), std::span{buffer}),
        stopped_receiver{{state}, &source});
    std::execution::start(op);

    {
        std::unique_lock lk{state->mtx};
        EXPECT_FALSE(state->cv.wait_for(lk, 50ms, [&] { return state->done(); }));
    }

    source.request_stop();

    ASSERT_TRUE(wait_done(state));
    EXPECT_FALSE(state->value);
    EXPECT_TRUE(state->stopped);
    EXPECT_FALSE(state->error);
}

TEST(IoIocpTest, CancelHandleCancelsPendingRead) {
    auto pipe = make_pipe_pair();
    forge::io::context ctx;
    std::array<std::byte, 8> buffer{};
    auto state = std::make_shared<io_state>();

    auto op = std::execution::connect(
        ctx.async_read_some(pipe.server.get(), std::span{buffer}),
        io_receiver{state});
    std::execution::start(op);

    ctx.cancel(pipe.server.get());

    ASSERT_TRUE(wait_done(state));
    EXPECT_FALSE(state->value);
    EXPECT_TRUE(state->stopped);
    EXPECT_FALSE(state->error);
}

TEST(IoIocpTest, ShutdownCancelsPendingRead) {
    auto pipe = make_pipe_pair();
    forge::io::context ctx;
    std::array<std::byte, 8> buffer{};
    auto state = std::make_shared<io_state>();

    auto op = std::execution::connect(
        ctx.async_read_some(pipe.server.get(), std::span{buffer}),
        io_receiver{state});
    std::execution::start(op);

    ctx.shutdown();

    ASSERT_TRUE(wait_done(state));
    EXPECT_FALSE(state->value);
    EXPECT_TRUE(state->stopped);
    EXPECT_FALSE(state->error);
    ctx.wait();
}

TEST(IoIocpTest, InvalidHandleCompletesWithError) {
    forge::io::context ctx;
    std::array<std::byte, 1> buffer{};

    EXPECT_THROW(
        (void)std::execution::sync_wait(
            ctx.async_read_some(INVALID_HANDLE_VALUE, std::span{buffer})),
        std::system_error);
}
