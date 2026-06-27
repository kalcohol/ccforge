#include <gtest/gtest.h>

#include <forge/io/context_await.hpp>
#include "forge_io_posix_fd.hpp"

#include <array>
#include <cstddef>
#include <execution>
#include <span>
#include <system_error>
#include <utility>

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L \
    && defined(FORGE_HAS_FORGE_IO_LINUX_EPOLL_BACKEND)
#include <unistd.h>
#endif

namespace {

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L \
    && defined(FORGE_HAS_FORGE_IO_LINUX_EPOLL_BACKEND)

using forge_test::make_pipe;

#endif

} // namespace

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L \
    && defined(FORGE_HAS_FORGE_IO_LINUX_EPOLL_BACKEND)

TEST(ForgeContextAwaitTest, AsyncReadWriteSomeReturnIoResult) {
    forge::io::context context;
    auto pipe = make_pipe();
    std::array<char, 3> payload{'a', 'b', 'c'};

    auto write_result = std::execution::sync_wait(
        forge::io::as_sender(
            forge::io::async_write_some(
                context,
                pipe.second.get(),
                std::as_bytes(std::span{payload}))));
    ASSERT_TRUE(write_result.has_value());
    auto [write_io] = std::move(*write_result);
    auto [write_error, written] = write_io;
    EXPECT_FALSE(write_error);
    EXPECT_EQ(written, payload.size());

    std::array<std::byte, 3> input{};
    auto read_result = std::execution::sync_wait(
        forge::io::as_sender(
            forge::io::async_read_some(
                context,
                pipe.first.get(),
                std::span{input})));
    ASSERT_TRUE(read_result.has_value());
    auto [read_io] = std::move(*read_result);
    auto [read_error, read_count] = read_io;
    EXPECT_FALSE(read_error);
    ASSERT_EQ(read_count, input.size());
    EXPECT_EQ(std::to_integer<char>(input[0]), 'a');
    EXPECT_EQ(std::to_integer<char>(input[1]), 'b');
    EXPECT_EQ(std::to_integer<char>(input[2]), 'c');
}

TEST(ForgeContextAwaitTest, EmptyReadPreservesBackendBehavior) {
    forge::io::context context;
    auto pipe = make_pipe();
    std::array<std::byte, 1> input{};

    auto read_result = std::execution::sync_wait(
        forge::io::as_sender(
            forge::io::async_read_some(
                context,
                pipe.first.get(),
                std::span{input}.first(0))));

    ASSERT_TRUE(read_result.has_value());
    auto [read_io] = std::move(*read_result);
    auto [error, count] = read_io;
    EXPECT_FALSE(error);
    EXPECT_EQ(count, 0u);
    EXPECT_FALSE(read_io.eof());
}

TEST(ForgeContextAwaitTest, PeerCloseMapsReadZeroToEof) {
    forge::io::context context;
    auto pipe = make_pipe();
    pipe.second.reset();
    std::array<std::byte, 1> input{};

    auto read_result = std::execution::sync_wait(
        forge::io::as_sender(
            forge::io::async_read_some(
                context,
                pipe.first.get(),
                std::span{input})));

    ASSERT_TRUE(read_result.has_value());
    auto [read_io] = std::move(*read_result);
    auto [error, count] = read_io;
    EXPECT_FALSE(error);
    EXPECT_TRUE(read_io.eof());
    EXPECT_EQ(count, 0u);
}

TEST(ForgeContextAwaitTest, InvalidHandleMapsToIoResultError) {
    forge::io::context context;
    std::array<std::byte, 1> input{};

    auto read_result = std::execution::sync_wait(
        forge::io::as_sender(
            forge::io::async_read_some(
                context,
                -1,
                std::span{input})));

    ASSERT_TRUE(read_result.has_value());
    auto [read_io] = std::move(*read_result);
    auto [error, count] = read_io;
    EXPECT_EQ(error, std::make_error_code(std::errc::bad_file_descriptor));
    EXPECT_EQ(count, 0u);
}

TEST(ForgeContextAwaitTest, ReadinessAwaitableHasNoByteCount) {
    forge::io::context context;
    auto pipe = make_pipe();
    const char byte = 'x';
    ASSERT_EQ(::write(pipe.second.get(), &byte, 1), 1);

    auto ready_result = std::execution::sync_wait(
        forge::io::as_sender(
            forge::io::readable(context, pipe.first.get())));

    ASSERT_TRUE(ready_result.has_value());
    auto [ready_io] = std::move(*ready_result);
    auto [error] = ready_io;
    EXPECT_FALSE(error);
}

TEST(ForgeContextAwaitTest, WritableReadinessAwaitableHasNoByteCount) {
    forge::io::context context;
    auto pipe = make_pipe();

    auto ready_result = std::execution::sync_wait(
        forge::io::as_sender(
            forge::io::writable(context, pipe.second.get())));

    ASSERT_TRUE(ready_result.has_value());
    auto [ready_io] = std::move(*ready_result);
    auto [error] = ready_io;
    EXPECT_FALSE(error);
}

TEST(ForgeContextAwaitTest, ReadinessObservesCoroutineEnvStopToken) {
    forge::io::context context;
    auto pipe = make_pipe();
    std::inplace_stop_source source;
    source.request_stop();

    forge::io::io_env env;
    env.stop_token = source.get_token();

    auto ready_result = std::execution::sync_wait(
        forge::io::as_sender(
            forge::io::readable(context, pipe.first.get()),
            env));

    EXPECT_FALSE(ready_result.has_value());
}

#else

TEST(ForgeContextAwaitTest, BackendUnavailable) {
    GTEST_SKIP()
        << "forge::io context await facade needs coroutine and Linux backend";
}

#endif
