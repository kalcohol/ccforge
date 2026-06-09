#include <gtest/gtest.h>
#include <forge/erased_sender.hpp>
#include <forge/io.hpp>
#include <forge/wait_result.hpp>
#include "forge_counting_resource.hpp"
#include "forge_operation_destroy.hpp"
#include <execution>
#include <array>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <fcntl.h>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <system_error>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <utility>

namespace {

using namespace std::chrono_literals;

class unique_fd {
public:
    unique_fd() noexcept = default;
    explicit unique_fd(int fd) noexcept : fd_(fd) {}
    ~unique_fd() noexcept { reset(); }

    unique_fd(unique_fd&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
    auto operator=(unique_fd&& other) noexcept -> unique_fd& {
        if (this != &other) {
            reset(std::exchange(other.fd_, -1));
        }
        return *this;
    }

    unique_fd(const unique_fd&) = delete;
    auto operator=(const unique_fd&) -> unique_fd& = delete;

    [[nodiscard]] auto get() const noexcept -> int { return fd_; }

    void reset(int next = -1) noexcept {
        if (fd_ >= 0) {
            ::close(fd_);
        }
        fd_ = next;
    }

private:
    int fd_ = -1;
};

struct fd_pair {
    unique_fd first;
    unique_fd second;
};

auto make_pipe() -> fd_pair {
    int fds[2]{-1, -1};
    if (::pipe2(fds, O_NONBLOCK | O_CLOEXEC) != 0) {
        throw std::runtime_error{"pipe2 failed"};
    }
    return fd_pair{unique_fd{fds[0]}, unique_fd{fds[1]}};
}

auto make_socketpair() -> fd_pair {
    int fds[2]{-1, -1};
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, fds) != 0) {
        throw std::runtime_error{"socketpair failed"};
    }
    return fd_pair{unique_fd{fds[0]}, unique_fd{fds[1]}};
}

void write_byte(int fd) {
    const char value = 'x';
    ASSERT_EQ(::write(fd, &value, 1), 1);
}

void fill_socket_send_buffer(int fd) {
    std::array<char, 4096> data{};
    while (true) {
        const auto result = ::send(fd, data.data(), data.size(), MSG_NOSIGNAL);
        if (result > 0) {
            continue;
        }
        if (result < 0 && errno == EINTR) {
            continue;
        }
        if (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return;
        }
        throw std::runtime_error{"send fill failed"};
    }
}

struct io_state {
    std::mutex mtx;
    std::condition_variable cv;
    bool value = false;
    bool stopped = false;
    std::exception_ptr error;

    [[nodiscard]] bool done() const noexcept {
        return value || stopped || error;
    }
};

struct io_receiver {
    using receiver_concept = std::execution::receiver_t;

    std::shared_ptr<io_state> state;

    void set_value() && noexcept {
        {
            std::lock_guard lk{state->mtx};
            state->value = true;
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

struct typed_io_state {
    std::mutex mtx;
    std::condition_variable cv;
    bool value = false;
    bool stopped = false;
    bool error = false;
    std::size_t bytes = 0;
    forge::io::error typed_error{};

    [[nodiscard]] bool done() const noexcept {
        return value || stopped || error;
    }
};

struct typed_void_receiver {
    using receiver_concept = std::execution::receiver_t;

    std::shared_ptr<typed_io_state> state;

    void set_value() && noexcept {
        {
            std::lock_guard lk{state->mtx};
            state->value = true;
        }
        state->cv.notify_all();
    }

    void set_error(forge::io::error error) && noexcept {
        {
            std::lock_guard lk{state->mtx};
            state->error = true;
            state->typed_error = error;
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

struct typed_size_receiver : typed_void_receiver {
    void set_value(std::size_t bytes) && noexcept {
        {
            std::lock_guard lk{state->mtx};
            state->value = true;
            state->bytes = bytes;
        }
        state->cv.notify_all();
    }
};

struct self_destroying_typed_io_receiver {
    using receiver_concept = std::execution::receiver_t;

    forge_test::destroy_context_base* context = nullptr;

    void set_value() && noexcept { context->destroy(); }
    void set_value(std::size_t) && noexcept { context->destroy(); }
    void set_error(forge::io::error) && noexcept { context->destroy(); }
    void set_stopped() && noexcept { context->destroy(); }
    auto get_env() const noexcept -> std::execution::empty_env { return {}; }
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

struct self_destroying_io_receiver {
    using receiver_concept = std::execution::receiver_t;

    forge_test::destroy_context_base* context = nullptr;

    void set_value() && noexcept { context->destroy(); }
    void set_error(std::exception_ptr) && noexcept { context->destroy(); }
    void set_stopped() && noexcept { context->destroy(); }
    auto get_env() const noexcept -> std::execution::empty_env { return {}; }
};

auto wait_done(const std::shared_ptr<io_state>& state) -> bool {
    std::unique_lock lk{state->mtx};
    return state->cv.wait_for(lk, 2s, [&] { return state->done(); });
}

auto wait_done(const std::shared_ptr<typed_io_state>& state) -> bool {
    std::unique_lock lk{state->mtx};
    return state->cv.wait_for(lk, 2s, [&] { return state->done(); });
}

} // namespace

static_assert(std::execution::sender<
    decltype(std::declval<forge::io::context&>().readable_typed(0))>);
static_assert(std::execution::sender<
    decltype(std::declval<forge::io::context&>().writable_typed(0))>);
static_assert(std::execution::sender<
    decltype(std::declval<forge::io::context&>().async_read_some_typed(
        0,
        std::declval<std::span<std::byte>>()))>);
static_assert(std::execution::sender<
    decltype(std::declval<forge::io::context&>().async_write_some_typed(
        0,
        std::declval<std::span<const std::byte>>()))>);

TEST(IoContextTest, EmptyContextDestroysCleanly) {
    forge::io::context ctx;
    ctx.shutdown();
    ctx.wait();
}

TEST(IoContextTest, OptionsConstructorUsesCustomMemoryResource) {
    forge_test::counting_resource resource;

    {
        forge::io::context ctx{forge::io::context_options{
            .memory = &resource,
            .max_events = 8,
        }};
        EXPECT_GT(resource.allocations(), 0u);
    }

    EXPECT_EQ(resource.allocations(), resource.deallocations());
}

TEST(IoContextTest, ReadableCompletesWhenPipeHasData) {
    auto pipe = make_pipe();
    forge::io::context ctx;

    write_byte(pipe.second.get());
    auto result = std::execution::sync_wait(ctx.readable(pipe.first.get()));

    EXPECT_TRUE(result.has_value());
}

TEST(IoContextTest, PendingReadableCompletesAfterWrite) {
    auto pipe = make_pipe();
    forge::io::context ctx;
    auto state = std::make_shared<io_state>();

    auto sender = ctx.readable(pipe.first.get());
    auto op = std::execution::connect(std::move(sender), io_receiver{state});
    std::execution::start(op);

    {
        std::unique_lock lk{state->mtx};
        EXPECT_FALSE(state->cv.wait_for(lk, 20ms, [&] { return state->done(); }));
    }

    write_byte(pipe.second.get());
    ASSERT_TRUE(wait_done(state));
    EXPECT_TRUE(state->value);
    EXPECT_FALSE(state->stopped);
    EXPECT_FALSE(state->error);
}

TEST(IoContextTest, CloseAfterReadinessStillCompletesTakenRecord) {
    for (int i = 0; i < 32; ++i) {
        auto pipe = make_pipe();
        forge::io::context ctx;
        auto state = std::make_shared<io_state>();

        auto sender = ctx.readable(pipe.first.get());
        auto op = std::execution::connect(std::move(sender), io_receiver{state});
        std::execution::start(op);

        write_byte(pipe.second.get());
        ctx.close();

        ASSERT_TRUE(wait_done(state));
        EXPECT_TRUE(state->value);
        EXPECT_FALSE(state->stopped);
        EXPECT_FALSE(state->error);
    }
}

TEST(IoContextTest, WritableCompletesForSocketpair) {
    auto sockets = make_socketpair();
    forge::io::context ctx;

    auto result = std::execution::sync_wait(ctx.writable(sockets.first.get()));

    EXPECT_TRUE(result.has_value());
}

TEST(IoContextTest, AsyncReadSomeReturnsByteCountAndData) {
    auto pipe = make_pipe();
    forge::io::context ctx;
    const char payload[] = {'a', 'b', 'c'};
    ASSERT_EQ(::write(pipe.second.get(), payload, sizeof(payload)),
              static_cast<ssize_t>(sizeof(payload)));

    std::array<std::byte, sizeof(payload)> buffer{};
    auto result = std::execution::sync_wait(
        ctx.async_read_some(pipe.first.get(), std::span{buffer}));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), buffer.size());
    EXPECT_EQ(buffer[0], std::byte{'a'});
    EXPECT_EQ(buffer[1], std::byte{'b'});
    EXPECT_EQ(buffer[2], std::byte{'c'});
}

TEST(IoContextTest, AsyncReadSomeZeroLengthReturnsZero) {
    auto pipe = make_pipe();
    forge::io::context ctx;
    ASSERT_EQ(::write(pipe.second.get(), "x", 1), 1);

    std::array<std::byte, 1> buffer{};
    auto result = std::execution::sync_wait(
        ctx.async_read_some(pipe.first.get(), std::span{buffer}.first(0)));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 0u);
}

TEST(IoContextTest, AsyncReadSomeReturnsZeroAtEof) {
    auto pipe = make_pipe();
    pipe.second.reset();

    forge::io::context ctx;
    std::array<std::byte, 1> buffer{};
    auto result = std::execution::sync_wait(
        ctx.async_read_some(pipe.first.get(), std::span{buffer}));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 0u);
}

TEST(IoContextTest, ReadinessToSyscallRaceSurfacesWouldBlock) {
    auto pipe = make_pipe();
    forge::io::context ctx;
    write_byte(pipe.second.get());

    auto work = ctx.readable(pipe.first.get())
              | std::execution::then([fd = pipe.first.get()] {
                    char discard{};
                    if (::read(fd, &discard, 1) != 1) {
                        throw std::runtime_error{"initial read failed"};
                    }

                    char again{};
                    if (::read(fd, &again, 1) >= 0) {
                        throw std::runtime_error{"unexpected second read"};
                    }
                    throw std::system_error{
                        errno, std::generic_category(), "second read"};
                });

    try {
        (void)std::execution::sync_wait(std::move(work));
        FAIL() << "expected second read to report would-block";
    } catch (const std::system_error& e) {
        const std::error_code expected{EAGAIN, std::generic_category()};
        EXPECT_EQ(e.code(), expected);
    }
}

TEST(IoContextTest, AsyncWriteSomeReturnsByteCountAndData) {
    auto pipe = make_pipe();
    forge::io::context ctx;
    std::array<char, 3> payload{'x', 'y', 'z'};

    auto result = std::execution::sync_wait(
        ctx.async_write_some(
            pipe.second.get(),
            std::as_bytes(std::span{payload})));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), payload.size());

    std::array<char, 3> received{};
    ASSERT_EQ(::read(pipe.first.get(), received.data(), received.size()),
              static_cast<ssize_t>(received.size()));
    EXPECT_EQ(received, payload);
}

TEST(IoContextTest, TypedReadableReportsDuplicateWaiter) {
    auto pipe = make_pipe();
    forge::io::context ctx;
    auto first = std::make_shared<typed_io_state>();
    auto second = std::make_shared<typed_io_state>();

    auto op1 = std::execution::connect(
        ctx.readable_typed(pipe.first.get()),
        typed_void_receiver{first});
    auto op2 = std::execution::connect(
        ctx.readable_typed(pipe.first.get()),
        typed_void_receiver{second});

    std::execution::start(op1);
    std::execution::start(op2);

    ASSERT_TRUE(wait_done(second));
    EXPECT_FALSE(second->value);
    EXPECT_FALSE(second->stopped);
    ASSERT_TRUE(second->error);
    EXPECT_EQ(second->typed_error.kind, forge::io::error_kind::operation_in_progress);
    EXPECT_EQ(second->typed_error.code,
              std::make_error_code(std::errc::operation_in_progress));

    ctx.cancel(pipe.first.get());
    ASSERT_TRUE(wait_done(first));
    EXPECT_TRUE(first->stopped);
}

TEST(IoContextTest, TypedReadableReportsBadFileDescriptor) {
    forge::io::context ctx;
    auto state = std::make_shared<typed_io_state>();

    auto op = std::execution::connect(
        ctx.readable_typed(-1),
        typed_void_receiver{state});
    std::execution::start(op);

    ASSERT_TRUE(wait_done(state));
    EXPECT_FALSE(state->value);
    EXPECT_FALSE(state->stopped);
    ASSERT_TRUE(state->error);
    EXPECT_EQ(state->typed_error.kind, forge::io::error_kind::invalid_handle);
    EXPECT_EQ(state->typed_error.code,
              std::make_error_code(std::errc::bad_file_descriptor));
}

TEST(IoContextTest, TypedReadableCrossesErasedSenderBoundary) {
    forge::io::context ctx;
    auto state = std::make_shared<typed_io_state>();
    using cs = std::execution::completion_signatures<
        std::execution::set_value_t(),
        std::execution::set_error_t(forge::io::error),
        std::execution::set_stopped_t()>;
    forge::erased_sender<cs> sender{ctx.readable_typed(-1)};

    auto op = std::execution::connect(
        std::move(sender),
        typed_void_receiver{state});
    std::execution::start(op);

    ASSERT_TRUE(wait_done(state));
    ASSERT_TRUE(state->error);
    EXPECT_EQ(state->typed_error.kind, forge::io::error_kind::invalid_handle);
}

TEST(IoContextTest, TypedReadableErrorCrossesWaitResult) {
    forge::io::context ctx;

    auto result = forge::wait_result(ctx.readable_typed(-1));

    ASSERT_TRUE(result.has_error());
    auto* error = result.error_if<forge::io::error>();
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->kind, forge::io::error_kind::invalid_handle);
    EXPECT_EQ(error->code, std::make_error_code(std::errc::bad_file_descriptor));
}

TEST(IoContextTest, TypedWritableReportsDuplicateWaiter) {
    auto sockets = make_socketpair();
    fill_socket_send_buffer(sockets.first.get());
    forge::io::context ctx;
    auto first = std::make_shared<typed_io_state>();
    auto second = std::make_shared<typed_io_state>();

    auto op1 = std::execution::connect(
        ctx.writable_typed(sockets.first.get()),
        typed_void_receiver{first});
    auto op2 = std::execution::connect(
        ctx.writable_typed(sockets.first.get()),
        typed_void_receiver{second});

    std::execution::start(op1);
    std::execution::start(op2);

    ASSERT_TRUE(wait_done(second));
    EXPECT_FALSE(second->value);
    EXPECT_FALSE(second->stopped);
    ASSERT_TRUE(second->error);
    EXPECT_EQ(second->typed_error.kind, forge::io::error_kind::operation_in_progress);
    EXPECT_EQ(second->typed_error.code,
              std::make_error_code(std::errc::operation_in_progress));

    ctx.cancel(sockets.first.get());
    ASSERT_TRUE(wait_done(first));
    EXPECT_TRUE(first->stopped);
}

TEST(IoContextTest, TypedWritableReportsBadFileDescriptor) {
    forge::io::context ctx;
    auto state = std::make_shared<typed_io_state>();

    auto op = std::execution::connect(
        ctx.writable_typed(-1),
        typed_void_receiver{state});
    std::execution::start(op);

    ASSERT_TRUE(wait_done(state));
    EXPECT_FALSE(state->value);
    EXPECT_FALSE(state->stopped);
    ASSERT_TRUE(state->error);
    EXPECT_EQ(state->typed_error.kind, forge::io::error_kind::invalid_handle);
    EXPECT_EQ(state->typed_error.code,
              std::make_error_code(std::errc::bad_file_descriptor));
}

TEST(IoContextTest, TypedWritableCrossesErasedSenderBoundary) {
    forge::io::context ctx;
    auto state = std::make_shared<typed_io_state>();
    using cs = std::execution::completion_signatures<
        std::execution::set_value_t(),
        std::execution::set_error_t(forge::io::error),
        std::execution::set_stopped_t()>;
    forge::erased_sender<cs> sender{ctx.writable_typed(-1)};

    auto op = std::execution::connect(
        std::move(sender),
        typed_void_receiver{state});
    std::execution::start(op);

    ASSERT_TRUE(wait_done(state));
    ASSERT_TRUE(state->error);
    EXPECT_EQ(state->typed_error.kind, forge::io::error_kind::invalid_handle);
}

TEST(IoContextTest, AsyncReadSomeTypedReturnsByteCount) {
    auto pipe = make_pipe();
    forge::io::context ctx;
    const char payload[] = {'t', 'y'};
    ASSERT_EQ(::write(pipe.second.get(), payload, sizeof(payload)),
              static_cast<ssize_t>(sizeof(payload)));
    std::array<std::byte, sizeof(payload)> buffer{};
    auto state = std::make_shared<typed_io_state>();

    auto op = std::execution::connect(
        ctx.async_read_some_typed(pipe.first.get(), std::span{buffer}),
        typed_size_receiver{state});
    std::execution::start(op);

    ASSERT_TRUE(wait_done(state));
    EXPECT_TRUE(state->value);
    EXPECT_FALSE(state->stopped);
    EXPECT_FALSE(state->error);
    EXPECT_EQ(state->bytes, buffer.size());
    EXPECT_EQ(buffer[0], std::byte{'t'});
    EXPECT_EQ(buffer[1], std::byte{'y'});
}

TEST(IoContextTest, AsyncWriteSomeTypedReturnsByteCount) {
    auto pipe = make_pipe();
    forge::io::context ctx;
    std::array<char, 3> payload{'w', 'r', 't'};
    auto state = std::make_shared<typed_io_state>();

    auto op = std::execution::connect(
        ctx.async_write_some_typed(
            pipe.second.get(),
            std::as_bytes(std::span{payload})),
        typed_size_receiver{state});
    std::execution::start(op);

    ASSERT_TRUE(wait_done(state));
    EXPECT_TRUE(state->value);
    EXPECT_FALSE(state->stopped);
    EXPECT_FALSE(state->error);
    EXPECT_EQ(state->bytes, payload.size());

    std::array<char, 3> received{};
    ASSERT_EQ(::read(pipe.first.get(), received.data(), received.size()),
              static_cast<ssize_t>(received.size()));
    EXPECT_EQ(received, payload);
}

TEST(IoContextTest, TypedWouldBlockClassificationFromException) {
    auto ep = std::make_exception_ptr(std::system_error{
        std::make_error_code(std::errc::resource_unavailable_try_again),
        "would block"});

    auto error = forge::io::typed_detail::from_exception(ep);

    EXPECT_EQ(error.kind, forge::io::error_kind::would_block);
    EXPECT_EQ(error.code,
              std::make_error_code(std::errc::resource_unavailable_try_again));
}

TEST(IoContextTest, RequestStopCancelsPendingWaiter) {
    auto pipe = make_pipe();
    forge::io::context ctx;
    auto state = std::make_shared<io_state>();

    auto op = std::execution::connect(
        ctx.readable(pipe.first.get()),
        io_receiver{state});
    std::execution::start(op);

    ctx.request_stop();

    ASSERT_TRUE(wait_done(state));
    EXPECT_FALSE(state->value);
    EXPECT_TRUE(state->stopped);
    EXPECT_FALSE(state->error);
}

TEST(IoContextTest, CancelFdStopsPendingWaiter) {
    auto pipe = make_pipe();
    forge::io::context ctx;
    auto state = std::make_shared<io_state>();

    auto op = std::execution::connect(
        ctx.readable(pipe.first.get()),
        io_receiver{state});
    std::execution::start(op);

    ctx.cancel(pipe.first.get());

    ASSERT_TRUE(wait_done(state));
    EXPECT_FALSE(state->value);
    EXPECT_TRUE(state->stopped);
    EXPECT_FALSE(state->error);
}

TEST(IoContextTest, DuplicateWaiterCompletesSecondWithError) {
    auto pipe = make_pipe();
    forge::io::context ctx;
    auto first = std::make_shared<io_state>();
    auto second = std::make_shared<io_state>();

    auto op1 = std::execution::connect(
        ctx.readable(pipe.first.get()),
        io_receiver{first});
    auto op2 = std::execution::connect(
        ctx.readable(pipe.first.get()),
        io_receiver{second});

    std::execution::start(op1);
    std::execution::start(op2);

    ASSERT_TRUE(wait_done(second));
    EXPECT_FALSE(second->value);
    EXPECT_FALSE(second->stopped);
    EXPECT_TRUE(second->error);

    ctx.cancel(pipe.first.get());
    ASSERT_TRUE(wait_done(first));
    EXPECT_TRUE(first->stopped);
}

TEST(IoContextTest, PreStoppedReceiverDoesNotRegisterFd) {
    auto pipe = make_pipe();
    forge::io::context ctx;
    auto state = std::make_shared<io_state>();
    std::inplace_stop_source source;
    source.request_stop();

    auto op = std::execution::connect(
        ctx.readable(pipe.first.get()),
        stopped_receiver{{state}, &source});
    std::execution::start(op);

    ASSERT_TRUE(wait_done(state));
    EXPECT_FALSE(state->value);
    EXPECT_TRUE(state->stopped);
    EXPECT_FALSE(state->error);
}

TEST(IoContextTest, PostEnqueueReceiverStopCompletesPendingReadable) {
    auto pipe = make_pipe();
    forge::io::context ctx;
    auto state = std::make_shared<io_state>();
    std::inplace_stop_source source;

    auto op = std::execution::connect(
        ctx.readable(pipe.first.get()),
        stopped_receiver{{state}, &source});
    std::execution::start(op);

    {
        std::unique_lock lk{state->mtx};
        EXPECT_FALSE(state->cv.wait_for(lk, 20ms, [&] { return state->done(); }));
    }

    source.request_stop();

    ASSERT_TRUE(wait_done(state));
    EXPECT_FALSE(state->value);
    EXPECT_TRUE(state->stopped);
    EXPECT_FALSE(state->error);

    write_byte(pipe.second.get());
    auto result = std::execution::sync_wait(ctx.readable(pipe.first.get()));
    EXPECT_TRUE(result.has_value());
}

TEST(IoContextTest, PostEnqueueReceiverStopCompletesPendingWritable) {
    auto sockets = make_socketpair();
    fill_socket_send_buffer(sockets.first.get());

    forge::io::context ctx;
    auto state = std::make_shared<io_state>();
    std::inplace_stop_source source;

    auto op = std::execution::connect(
        ctx.writable(sockets.first.get()),
        stopped_receiver{{state}, &source});
    std::execution::start(op);

    source.request_stop();

    ASSERT_TRUE(wait_done(state));
    EXPECT_FALSE(state->value);
    EXPECT_TRUE(state->stopped);
    EXPECT_FALSE(state->error);
}

TEST(IoContextTest, InvalidFdCompletesWithError) {
    forge::io::context ctx;

    EXPECT_THROW(
        (void)std::execution::sync_wait(ctx.readable(-1)),
        std::system_error);
}

TEST(IoContextTest, InvalidFdAllowsReceiverToDestroyOperation) {
    forge::io::context ctx;

    using sender_t = decltype(ctx.readable(-1));
    using receiver_t = self_destroying_io_receiver;
    using op_t = decltype(std::execution::connect(
        std::declval<sender_t>(),
        std::declval<receiver_t>()));

    bool destroyed = false;
    forge_test::operation_destroy_context<op_t> context{&destroyed};

    auto& op = context.emplace_from([&] {
        return std::execution::connect(
            ctx.readable(-1),
            self_destroying_io_receiver{&context});
    });
    std::execution::start(op);

    EXPECT_TRUE(destroyed);
    EXPECT_FALSE(context.has_value);
}

TEST(IoContextTest, TypedInvalidFdAllowsReceiverToDestroyOperation) {
    forge::io::context ctx;

    using sender_t = decltype(ctx.readable_typed(-1));
    using receiver_t = self_destroying_typed_io_receiver;
    using op_t = decltype(std::execution::connect(
        std::declval<sender_t>(),
        std::declval<receiver_t>()));

    bool destroyed = false;
    forge_test::operation_destroy_context<op_t> context{&destroyed};

    auto& op = context.emplace_from([&] {
        return std::execution::connect(
            ctx.readable_typed(-1),
            self_destroying_typed_io_receiver{&context});
    });
    std::execution::start(op);

    EXPECT_TRUE(destroyed);
    EXPECT_FALSE(context.has_value);
}
