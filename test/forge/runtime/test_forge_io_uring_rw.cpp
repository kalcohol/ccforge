#include <forge/io/io_uring_context.hpp>

#include <forge/io/async_stream.hpp>

#include "forge_counting_resource.hpp"

#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <execution>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stop_token>
#include <system_error>
#include <thread>
#include <utility>

namespace {

constexpr int skip_exit_code = 77;

[[nodiscard]] auto runtime_unavailable(const std::error_code& error) noexcept
    -> bool {
    return error.value() == ENOSYS ||
           error.value() == EPERM ||
           error.value() == EACCES ||
           error.value() == EOPNOTSUPP ||
           error.value() == ENOMEM;
}

} // namespace

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L

namespace {

namespace cio = forge::io;
using namespace std::chrono_literals;

int failures = 0;

#define RW_CHECK(condition)                                                  \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::fprintf(                                                    \
                stderr,                                                      \
                "check failed at %s:%d: %s\n",                               \
                __FILE__,                                                    \
                __LINE__,                                                    \
                #condition);                                                 \
            ++failures;                                                      \
        }                                                                    \
    } while (false)

struct pipe_pair {
    int read_end = -1;
    int write_end = -1;

    pipe_pair() {
        int fds[2] = {-1, -1};
        if (::pipe(fds) != 0) {
            std::perror("pipe");
        } else {
            read_end = fds[0];
            write_end = fds[1];
        }
    }

    ~pipe_pair() {
        close_read();
        close_write();
    }

    pipe_pair(const pipe_pair&) = delete;
    auto operator=(const pipe_pair&) -> pipe_pair& = delete;

    void close_read() noexcept {
        if (read_end >= 0) {
            ::close(read_end);
            read_end = -1;
        }
    }

    void close_write() noexcept {
        if (write_end >= 0) {
            ::close(write_end);
            write_end = -1;
        }
    }
};

auto read_task(
    cio::io_uring_context& context,
    int fd,
    std::span<std::byte> buffer) -> cio::io_task<cio::io_result<std::size_t>> {
    co_return co_await cio::async_read_some(context, fd, buffer);
}

auto write_task(
    cio::io_uring_context& context,
    int fd,
    std::span<const std::byte> buffer)
    -> cio::io_task<cio::io_result<std::size_t>> {
    co_return co_await cio::async_write_some(context, fd, buffer);
}

auto observe_read(
    cio::io_uring_context& context,
    int fd,
    std::span<std::byte> buffer,
    std::thread::id* before,
    std::thread::id* after) -> cio::io_task<cio::io_result<std::size_t>> {
    *before = std::this_thread::get_id();
    auto result = co_await cio::async_read_some(context, fd, buffer);
    *after = std::this_thread::get_id();
    co_return result;
}

// Test-local adapter proving the io_uring awaitables satisfy the direct
// async stream concepts from taskbook 03. The awaitable deliberately does
// not fit the frozen 128-byte erasure slot, so owning_any_async_* wrappers
// reject it at compile time; the tripwire below revisits that boundary if
// the operation state ever shrinks.
struct uring_pipe_stream {
    cio::io_uring_context* context = nullptr;
    int read_fd = -1;
    int write_fd = -1;

    [[nodiscard]] auto read_some(cio::mutable_buffer output) {
        return cio::async_read_some(
            *context,
            read_fd,
            std::span<std::byte>{output.data(), output.size()});
    }

    [[nodiscard]] auto write_some(cio::const_buffer input) {
        return cio::async_write_some(
            *context,
            write_fd,
            std::span<const std::byte>{input.data(), input.size()});
    }
};

static_assert(cio::async_read_write_stream<uring_pipe_stream>);
static_assert(
    sizeof(cio::async_read_awaitable_t<uring_pipe_stream>) >
    cio::erased_io_awaitable_size);

struct completion_state {
    std::mutex mutex;
    std::condition_variable cv;
    std::optional<cio::io_result<std::size_t>> result;
    std::exception_ptr error;
    bool stopped = false;

    [[nodiscard]] auto done() const noexcept -> bool {
        return result.has_value() || error || stopped;
    }
};

struct completion_receiver {
    using receiver_concept = std::execution::receiver_t;

    std::shared_ptr<completion_state> state;

    // Notification happens under the mutex: the waiter may destroy the
    // completion_state as soon as it observes done(), so the broadcast must
    // be ordered before the waiter can reacquire the lock.
    auto set_value(cio::io_result<std::size_t> result) && noexcept -> void {
        std::lock_guard lock{state->mutex};
        state->result.emplace(std::move(result));
        state->cv.notify_all();
    }

    auto set_error(std::exception_ptr error) && noexcept -> void {
        std::lock_guard lock{state->mutex};
        state->error = std::move(error);
        state->cv.notify_all();
    }

    auto set_stopped() && noexcept -> void {
        std::lock_guard lock{state->mutex};
        state->stopped = true;
        state->cv.notify_all();
    }

    [[nodiscard]] auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }
};

[[nodiscard]] auto wait_done(
    const std::shared_ptr<completion_state>& state) -> bool {
    std::unique_lock lock{state->mutex};
    return state->cv.wait_for(lock, 5s, [&] { return state->done(); });
}

void check_write_then_read(std::pmr::memory_resource* memory) {
    cio::io_uring_context context{{.memory = memory, .entries = 8}};
    pipe_pair pipe;
    RW_CHECK(pipe.read_end >= 0);

    const char payload[] = "hello";
    std::byte write_buffer[5];
    std::memcpy(write_buffer, payload, sizeof(write_buffer));

    auto write_result = std::execution::sync_wait(cio::as_sender(
        write_task(context, pipe.write_end, write_buffer)));
    RW_CHECK(write_result.has_value());
    if (write_result.has_value()) {
        auto [io] = std::move(*write_result);
        RW_CHECK(io.has_value());
        RW_CHECK(std::get<0>(io.values()) == sizeof(write_buffer));
    }

    // A larger destination observes the natural short read.
    std::byte read_buffer[64] = {};
    auto read_result = std::execution::sync_wait(cio::as_sender(
        read_task(context, pipe.read_end, read_buffer)));
    RW_CHECK(read_result.has_value());
    if (read_result.has_value()) {
        auto [io] = std::move(*read_result);
        RW_CHECK(io.has_value());
        RW_CHECK(std::get<0>(io.values()) == sizeof(write_buffer));
        RW_CHECK(std::memcmp(read_buffer, payload, sizeof(write_buffer)) == 0);
    }
}

void check_pending_read_resumes_on_poller(std::pmr::memory_resource* memory) {
    cio::io_uring_context context{{.memory = memory, .entries = 8}};
    pipe_pair pipe;
    RW_CHECK(pipe.read_end >= 0);

    std::byte read_buffer[16] = {};
    std::thread::id before{};
    std::thread::id after{};
    const auto caller = std::this_thread::get_id();

    std::thread writer{[&] {
        std::this_thread::sleep_for(20ms);
        const char payload[] = "ping";
        (void)!::write(pipe.write_end, payload, 4);
    }};

    auto result = std::execution::sync_wait(cio::as_sender(
        observe_read(context, pipe.read_end, read_buffer, &before, &after)));
    writer.join();

    RW_CHECK(result.has_value());
    if (result.has_value()) {
        auto [io] = std::move(*result);
        RW_CHECK(io.has_value());
        RW_CHECK(std::get<0>(io.values()) == 4);
    }
    RW_CHECK(before == caller);
    RW_CHECK(after != caller);
}

void check_read_eof(std::pmr::memory_resource* memory) {
    cio::io_uring_context context{{.memory = memory, .entries = 8}};
    pipe_pair pipe;
    RW_CHECK(pipe.read_end >= 0);
    pipe.close_write();

    std::byte read_buffer[16] = {};
    auto result = std::execution::sync_wait(cio::as_sender(
        read_task(context, pipe.read_end, read_buffer)));
    RW_CHECK(result.has_value());
    if (result.has_value()) {
        auto [io] = std::move(*result);
        RW_CHECK(io.eof());
        RW_CHECK(!io.error());
    }
}

void check_empty_buffer_completes_inline(std::pmr::memory_resource* memory) {
    cio::io_uring_context context{{.memory = memory, .entries = 8}};
    pipe_pair pipe;
    RW_CHECK(pipe.read_end >= 0);

    auto read_result = std::execution::sync_wait(cio::as_sender(
        read_task(context, pipe.read_end, std::span<std::byte>{})));
    RW_CHECK(read_result.has_value());
    if (read_result.has_value()) {
        auto [io] = std::move(*read_result);
        RW_CHECK(io.has_value());
        RW_CHECK(!io.eof());
        RW_CHECK(std::get<0>(io.values()) == 0);
    }

    // The empty-buffer fast path never consults the context, so it still
    // completes with value 0 after close().
    context.close();
    auto closed_result = std::execution::sync_wait(cio::as_sender(
        write_task(
            context,
            pipe.write_end,
            std::span<const std::byte>{})));
    RW_CHECK(closed_result.has_value());
    if (closed_result.has_value()) {
        auto [io] = std::move(*closed_result);
        RW_CHECK(io.has_value());
        RW_CHECK(std::get<0>(io.values()) == 0);
    }
}

void check_bad_descriptor_maps_to_error(std::pmr::memory_resource* memory) {
    cio::io_uring_context context{{.memory = memory, .entries = 8}};
    pipe_pair pipe;
    RW_CHECK(pipe.read_end >= 0);

    const std::byte payload[1] = {};
    auto result = std::execution::sync_wait(cio::as_sender(
        write_task(context, pipe.read_end, payload)));
    RW_CHECK(result.has_value());
    if (result.has_value()) {
        auto [io] = std::move(*result);
        RW_CHECK(!io.has_value());
        RW_CHECK(io.error() == std::error_code(EBADF, std::generic_category()));
    }
}

// A peer-closed pipe must surface EPIPE through the result channel instead
// of killing the process: without the enter-side SIGPIPE guard this check
// dies with SIGPIPE before any assertion runs.
void check_write_to_closed_reader_maps_to_epipe(
    std::pmr::memory_resource* memory) {
    cio::io_uring_context context{{.memory = memory, .entries = 8}};
    pipe_pair pipe;
    RW_CHECK(pipe.read_end >= 0);
    pipe.close_read();

    const std::byte payload[4] = {};
    auto result = std::execution::sync_wait(cio::as_sender(
        write_task(context, pipe.write_end, payload)));
    RW_CHECK(result.has_value());
    if (result.has_value()) {
        auto [io] = std::move(*result);
        RW_CHECK(!io.has_value());
        RW_CHECK(io.error() == std::error_code(EPIPE, std::generic_category()));
    }
}

void check_pre_stopped_env_never_submits(std::pmr::memory_resource* memory) {
    cio::io_uring_context context{{.memory = memory, .entries = 8}};
    pipe_pair pipe;
    RW_CHECK(pipe.read_end >= 0);

    std::inplace_stop_source stop;
    stop.request_stop();
    cio::io_env env;
    env.stop_token = stop.get_token();

    std::byte read_buffer[16] = {};
    // Returning at all proves the read was never submitted: the pipe never
    // carries data, so a submitted read would keep this wait pending.
    auto result = std::execution::sync_wait(cio::as_sender(
        read_task(context, pipe.read_end, read_buffer),
        env));
    RW_CHECK(!result.has_value());
}

auto echo_round_trip(
    cio::io_uring_context& context,
    int write_fd,
    int read_fd,
    std::span<const std::byte> payload,
    std::span<std::byte> scratch) -> cio::io_task<cio::io_result<std::size_t>> {
    auto written = co_await cio::async_write_some(context, write_fd, payload);
    if (!written.has_value()) {
        co_return written;
    }
    co_return co_await cio::async_read_some(context, read_fd, scratch);
}

// Coroutine-native end-to-end path: several native awaits composed inside
// one io_task, driven through the explicit sender bridge exactly once.
void check_coroutine_composition(std::pmr::memory_resource* memory) {
    cio::io_uring_context context{{.memory = memory, .entries = 8}};
    pipe_pair pipe;
    RW_CHECK(pipe.read_end >= 0);

    const char payload[] = "compose";
    std::byte source[7];
    std::memcpy(source, payload, sizeof(source));
    std::byte scratch[32] = {};

    auto result = std::execution::sync_wait(cio::as_sender(
        echo_round_trip(
            context,
            pipe.write_end,
            pipe.read_end,
            source,
            scratch)));
    RW_CHECK(result.has_value());
    if (result.has_value()) {
        auto [io] = std::move(*result);
        RW_CHECK(io.has_value());
        RW_CHECK(std::get<0>(io.values()) == sizeof(source));
        RW_CHECK(std::memcmp(scratch, payload, sizeof(source)) == 0);
    }
}

auto stream_round_trip(
    uring_pipe_stream& stream,
    std::span<const std::byte> payload,
    std::span<std::byte> scratch) -> cio::io_task<cio::io_result<std::size_t>> {
    auto written = co_await stream.write_some(cio::const_buffer{payload});
    if (!written.has_value()) {
        co_return written;
    }
    co_return co_await stream.read_some(cio::mutable_buffer{scratch});
}

auto stream_round_trip_read_only(
    uring_pipe_stream& stream,
    std::span<std::byte> scratch) -> cio::io_task<cio::io_result<std::size_t>> {
    co_return co_await stream.read_some(cio::mutable_buffer{scratch});
}

void check_async_stream_concept_interop(std::pmr::memory_resource* memory) {
    cio::io_uring_context context{{.memory = memory, .entries = 8}};
    pipe_pair pipe;
    RW_CHECK(pipe.read_end >= 0);

    uring_pipe_stream stream{&context, pipe.read_end, pipe.write_end};
    const char payload[] = "stream";
    std::byte source[6];
    std::memcpy(source, payload, sizeof(source));
    std::byte scratch[16] = {};

    auto result = std::execution::sync_wait(cio::as_sender(
        stream_round_trip(stream, source, scratch)));
    RW_CHECK(result.has_value());
    if (result.has_value()) {
        auto [io] = std::move(*result);
        RW_CHECK(io.has_value());
        RW_CHECK(std::get<0>(io.values()) == sizeof(source));
        RW_CHECK(std::memcmp(scratch, payload, sizeof(source)) == 0);
    }

    // EOF surfaces through the adapter unchanged.
    pipe.close_write();
    auto eof_result = std::execution::sync_wait(cio::as_sender(
        stream_round_trip_read_only(stream, scratch)));
    RW_CHECK(eof_result.has_value());
    if (eof_result.has_value()) {
        auto [io] = std::move(*eof_result);
        RW_CHECK(io.eof());
    }
}

void check_pre_stopped_env_wins_over_empty_buffer(
    std::pmr::memory_resource* memory) {
    cio::io_uring_context context{{.memory = memory, .entries = 8}};
    pipe_pair pipe;
    RW_CHECK(pipe.read_end >= 0);

    std::inplace_stop_source stop;
    stop.request_stop();
    cio::io_env env;
    env.stop_token = stop.get_token();

    // The stop precheck is ordered before the empty-buffer fast path, so a
    // pre-stopped environment observes stopped rather than value 0.
    auto result = std::execution::sync_wait(cio::as_sender(
        read_task(context, pipe.read_end, std::span<std::byte>{}),
        env));
    RW_CHECK(!result.has_value());
}

void check_stop_races_natural_completion(std::pmr::memory_resource* memory) {
    cio::io_uring_context context{{.memory = memory, .entries = 8}};

    for (int i = 0; i < 50; ++i) {
        pipe_pair pipe;
        RW_CHECK(pipe.read_end >= 0);

        std::inplace_stop_source stop;
        cio::io_env env;
        env.stop_token = stop.get_token();
        auto state = std::make_shared<completion_state>();
        std::byte read_buffer[16] = {};

        auto operation = std::execution::connect(
            cio::as_sender(
                read_task(context, pipe.read_end, read_buffer),
                env),
            completion_receiver{state});
        std::execution::start(operation);

        // Whoever wins, the terminal must be the target CQE result: either
        // the delivered value or stopped, never a fabricated error.
        RW_CHECK(::write(pipe.write_end, "race", 4) == 4);
        stop.request_stop();

        RW_CHECK(wait_done(state));
        std::lock_guard lock{state->mutex};
        RW_CHECK(!state->error);
        if (state->result.has_value()) {
            RW_CHECK(state->result->has_value());
            RW_CHECK(std::get<0>(state->result->values()) == 4);
        } else {
            RW_CHECK(state->stopped);
        }
    }
}

void check_small_ring_concurrent_reads(std::pmr::memory_resource* memory) {
    cio::io_uring_context context{{.memory = memory, .entries = 1}};

    constexpr int lanes = 4;
    pipe_pair pipes[lanes];
    std::shared_ptr<completion_state> states[lanes];
    std::byte buffers[lanes][8] = {};

    for (int i = 0; i < lanes; ++i) {
        RW_CHECK(pipes[i].read_end >= 0);
        states[i] = std::make_shared<completion_state>();
    }

    auto first = std::execution::connect(
        cio::as_sender(read_task(context, pipes[0].read_end, buffers[0])),
        completion_receiver{states[0]});
    auto second = std::execution::connect(
        cio::as_sender(read_task(context, pipes[1].read_end, buffers[1])),
        completion_receiver{states[1]});
    auto third = std::execution::connect(
        cio::as_sender(read_task(context, pipes[2].read_end, buffers[2])),
        completion_receiver{states[2]});
    auto fourth = std::execution::connect(
        cio::as_sender(read_task(context, pipes[3].read_end, buffers[3])),
        completion_receiver{states[3]});
    std::execution::start(first);
    std::execution::start(second);
    std::execution::start(third);
    std::execution::start(fourth);

    for (int i = lanes - 1; i >= 0; --i) {
        const char payload = static_cast<char>('a' + i);
        RW_CHECK(::write(pipes[i].write_end, &payload, 1) == 1);
    }

    for (int i = 0; i < lanes; ++i) {
        RW_CHECK(wait_done(states[i]));
        std::lock_guard lock{states[i]->mutex};
        RW_CHECK(states[i]->result.has_value());
        if (states[i]->result.has_value()) {
            RW_CHECK(states[i]->result->has_value());
            RW_CHECK(std::get<0>(states[i]->result->values()) == 1);
        }
        RW_CHECK(static_cast<char>(buffers[i][0]) == 'a' + i);
    }
}

void check_stop_cancels_pending_read(std::pmr::memory_resource* memory) {
    cio::io_uring_context context{{.memory = memory, .entries = 8}};
    pipe_pair pipe;
    RW_CHECK(pipe.read_end >= 0);

    std::inplace_stop_source stop;
    cio::io_env env;
    env.stop_token = stop.get_token();
    auto state = std::make_shared<completion_state>();
    std::byte read_buffer[16] = {};

    auto operation = std::execution::connect(
        cio::as_sender(
            read_task(context, pipe.read_end, read_buffer),
            env),
        completion_receiver{state});
    // start() drives the coroutine to its suspension point, so the read is
    // registered with the ring before stop is requested.
    std::execution::start(operation);
    stop.request_stop();

    RW_CHECK(wait_done(state));
    {
        std::lock_guard lock{state->mutex};
        RW_CHECK(state->stopped);
        RW_CHECK(!state->result.has_value());
        RW_CHECK(!state->error);
    }
}

void check_request_stop_cancels_in_flight(std::pmr::memory_resource* memory) {
    cio::io_uring_context context{{.memory = memory, .entries = 8}};
    pipe_pair pipe;
    RW_CHECK(pipe.read_end >= 0);

    auto state = std::make_shared<completion_state>();
    std::byte read_buffer[16] = {};

    auto operation = std::execution::connect(
        cio::as_sender(read_task(context, pipe.read_end, read_buffer)),
        completion_receiver{state});
    std::execution::start(operation);
    context.request_stop();

    RW_CHECK(wait_done(state));
    {
        std::lock_guard lock{state->mutex};
        RW_CHECK(state->stopped);
    }

    // Ingress stays closed for later submissions.
    auto late = std::execution::sync_wait(cio::as_sender(
        read_task(context, pipe.read_end, read_buffer)));
    RW_CHECK(!late.has_value());
    context.wait();
}

void check_close_does_not_cancel(std::pmr::memory_resource* memory) {
    cio::io_uring_context context{{.memory = memory, .entries = 8}};
    pipe_pair pipe;
    RW_CHECK(pipe.read_end >= 0);

    auto state = std::make_shared<completion_state>();
    std::byte read_buffer[16] = {};

    auto operation = std::execution::connect(
        cio::as_sender(read_task(context, pipe.read_end, read_buffer)),
        completion_receiver{state});
    std::execution::start(operation);
    context.close();

    const char payload[] = "late";
    RW_CHECK(::write(pipe.write_end, payload, 4) == 4);

    RW_CHECK(wait_done(state));
    {
        std::lock_guard lock{state->mutex};
        RW_CHECK(state->result.has_value());
        if (state->result.has_value()) {
            RW_CHECK(state->result->has_value());
            RW_CHECK(std::get<0>(state->result->values()) == 4);
        }
    }
    context.wait();
}

void check_concurrent_reads_on_two_fds(std::pmr::memory_resource* memory) {
    cio::io_uring_context context{{.memory = memory, .entries = 8}};
    pipe_pair first;
    pipe_pair second;
    RW_CHECK(first.read_end >= 0);
    RW_CHECK(second.read_end >= 0);

    auto first_state = std::make_shared<completion_state>();
    auto second_state = std::make_shared<completion_state>();
    std::byte first_buffer[16] = {};
    std::byte second_buffer[16] = {};

    auto first_operation = std::execution::connect(
        cio::as_sender(read_task(context, first.read_end, first_buffer)),
        completion_receiver{first_state});
    auto second_operation = std::execution::connect(
        cio::as_sender(read_task(context, second.read_end, second_buffer)),
        completion_receiver{second_state});
    std::execution::start(first_operation);
    std::execution::start(second_operation);

    // Complete in reverse submission order.
    RW_CHECK(::write(second.write_end, "22", 2) == 2);
    RW_CHECK(::write(first.write_end, "1", 1) == 1);

    RW_CHECK(wait_done(first_state));
    RW_CHECK(wait_done(second_state));
    {
        std::lock_guard lock{first_state->mutex};
        RW_CHECK(first_state->result.has_value());
        if (first_state->result.has_value()) {
            RW_CHECK(std::get<0>(first_state->result->values()) == 1);
        }
    }
    {
        std::lock_guard lock{second_state->mutex};
        RW_CHECK(second_state->result.has_value());
        if (second_state->result.has_value()) {
            RW_CHECK(std::get<0>(second_state->result->values()) == 2);
        }
    }
    RW_CHECK(std::memcmp(first_buffer, "1", 1) == 0);
    RW_CHECK(std::memcmp(second_buffer, "22", 2) == 0);
}

// entries=1 gives a one-slot SQ and a two-entry CQ, so six in-flight reads
// force CQ overflow (EBUSY flush results) and park most of the batch
// cancels for the poller retry walk during shutdown.
void check_shutdown_batch_cancels_under_pressure(
    std::pmr::memory_resource* memory) {
    cio::io_uring_context context{{.memory = memory, .entries = 1}};

    constexpr int lanes = 6;
    pipe_pair pipes[lanes];
    std::shared_ptr<completion_state> states[lanes];
    std::byte buffers[lanes][8] = {};

    for (int i = 0; i < lanes; ++i) {
        RW_CHECK(pipes[i].read_end >= 0);
        states[i] = std::make_shared<completion_state>();
    }

    auto make_operation = [&](int i) {
        return std::execution::connect(
            cio::as_sender(read_task(context, pipes[i].read_end, buffers[i])),
            completion_receiver{states[i]});
    };
    auto first = make_operation(0);
    auto second = make_operation(1);
    auto third = make_operation(2);
    auto fourth = make_operation(3);
    auto fifth = make_operation(4);
    auto sixth = make_operation(5);
    std::execution::start(first);
    std::execution::start(second);
    std::execution::start(third);
    std::execution::start(fourth);
    std::execution::start(fifth);
    std::execution::start(sixth);

    context.shutdown();

    for (int i = 0; i < lanes; ++i) {
        RW_CHECK(wait_done(states[i]));
        std::lock_guard lock{states[i]->mutex};
        RW_CHECK(states[i]->stopped);
        RW_CHECK(!states[i]->error);
    }
    context.wait();
}

auto read_then_destroy(
    std::optional<cio::io_uring_context>& context,
    int fd,
    std::span<std::byte> buffer) -> cio::io_task<cio::io_result<std::size_t>> {
    auto result = co_await cio::async_read_some(*context, fd, buffer);
    // Runs on the poller thread: the destructor takes the self-join detach
    // path and the detached poller finishes the terminal tail on its own.
    context.reset();
    co_return result;
}

void check_destroy_context_from_completion(std::pmr::memory_resource* memory) {
    pipe_pair pipe;
    RW_CHECK(pipe.read_end >= 0);

    std::optional<cio::io_uring_context> context;
    context.emplace(
        cio::io_uring_context_options{.memory = memory, .entries = 8});

    std::byte read_buffer[16] = {};
    std::thread writer{[&] {
        std::this_thread::sleep_for(20ms);
        (void)!::write(pipe.write_end, "bye", 3);
    }};

    auto result = std::execution::sync_wait(cio::as_sender(
        read_then_destroy(context, pipe.read_end, read_buffer)));
    writer.join();

    RW_CHECK(!context.has_value());
    RW_CHECK(result.has_value());
    if (result.has_value()) {
        auto [io] = std::move(*result);
        RW_CHECK(io.has_value());
        RW_CHECK(std::get<0>(io.values()) == 3);
    }
}

void check_destructor_cancels_in_flight(std::pmr::memory_resource* memory) {
    pipe_pair pipe;
    RW_CHECK(pipe.read_end >= 0);

    auto state = std::make_shared<completion_state>();
    std::byte read_buffer[16] = {};

    std::optional<cio::io_uring_context> context;
    context.emplace(
        cio::io_uring_context_options{.memory = memory, .entries = 8});

    auto operation = std::execution::connect(
        cio::as_sender(read_task(*context, pipe.read_end, read_buffer)),
        completion_receiver{state});
    std::execution::start(operation);

    // Destroying the context runs shutdown() + wait(): the in-flight read
    // must be canceled and completed before the ring is torn down.
    context.reset();

    RW_CHECK(wait_done(state));
    {
        std::lock_guard lock{state->mutex};
        RW_CHECK(state->stopped);
        RW_CHECK(!state->result.has_value());
        RW_CHECK(!state->error);
    }
}

void check_last_error_stays_clear_on_graceful_stop(
    std::pmr::memory_resource* memory) {
    cio::io_uring_context context{{.memory = memory, .entries = 8}};
    RW_CHECK(!context.last_error());

    context.request_stop();
    context.wait();
    RW_CHECK(!context.last_error());
}

// Runs first: fork needs a single-threaded parent, and later checks leave a
// detached poller behind.
void check_abandoning_submitted_read_terminates(
    std::pmr::memory_resource* memory) {
    const pid_t child = ::fork();
    if (child < 0) {
        std::perror("fork");
        ++failures;
        return;
    }
    if (child == 0) {
        // Abandoning a submitted-but-unresumed operation would hand the
        // kernel a dangling frame and buffer; the awaitable destructor must
        // fail fast instead of corrupting memory later.
        cio::io_uring_context context{{.memory = memory, .entries = 8}};
        pipe_pair pipe;
        std::byte buffer[8] = {};
        auto state = std::make_shared<completion_state>();
        {
            auto operation = std::execution::connect(
                cio::as_sender(read_task(context, pipe.read_end, buffer)),
                completion_receiver{state});
            std::execution::start(operation);
        }
        // Reaching this point means the terminate guard did not fire.
        std::_Exit(0);
    }

    int status = 0;
    RW_CHECK(::waitpid(child, &status, 0) == child);
    RW_CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
}

} // namespace

int main() {
    forge_test::counting_resource memory;
    try {
        {
            cio::io_uring_context probe{{.memory = &memory, .entries = 8}};
        }
    } catch (const std::system_error& error) {
        std::fprintf(
            stderr,
            "io_uring context unavailable: %s (%d)\n",
            error.what(),
            error.code().value());
#if defined(FORGE_TEST_IO_URING_RUNTIME_REQUIRED)
        return 1;
#else
        return runtime_unavailable(error.code()) ? skip_exit_code : 1;
#endif
    }

    check_abandoning_submitted_read_terminates(&memory);
    check_last_error_stays_clear_on_graceful_stop(&memory);
    check_write_then_read(&memory);
    check_pending_read_resumes_on_poller(&memory);
    check_read_eof(&memory);
    check_empty_buffer_completes_inline(&memory);
    check_bad_descriptor_maps_to_error(&memory);
    check_write_to_closed_reader_maps_to_epipe(&memory);
    check_coroutine_composition(&memory);
    check_async_stream_concept_interop(&memory);
    check_pre_stopped_env_never_submits(&memory);
    check_pre_stopped_env_wins_over_empty_buffer(&memory);
    check_stop_races_natural_completion(&memory);
    check_small_ring_concurrent_reads(&memory);
    check_stop_cancels_pending_read(&memory);
    check_request_stop_cancels_in_flight(&memory);
    check_close_does_not_cancel(&memory);
    check_concurrent_reads_on_two_fds(&memory);
    check_shutdown_batch_cancels_under_pressure(&memory);
    check_destroy_context_from_completion(&memory);
    check_destructor_cancels_in_flight(&memory);

    // The detached poller from the completion-destruction check releases
    // the context state slightly after the awaiting side observes its
    // terminal, so give the final release tail a bounded grace window.
    for (int i = 0; i < 200 && memory.outstanding() != 0; ++i) {
        std::this_thread::sleep_for(10ms);
    }

    if (memory.allocations() == 0 || memory.outstanding() != 0) {
        std::fprintf(
            stderr,
            "unexpected PMR counts: allocations=%zu outstanding=%zu\n",
            memory.allocations(),
            memory.outstanding());
        return 3;
    }
    if (failures != 0) {
        std::fprintf(stderr, "%d checks failed\n", failures);
        return 1;
    }
    return 0;
}

#else

int main() {
    std::fprintf(stderr, "C++20 coroutines unavailable; skipping\n");
    return skip_exit_code;
}

#endif
