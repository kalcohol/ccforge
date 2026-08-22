// Failure-injection coverage for the io_uring submission-flush hardening.
// The binary links with -Wl,--wrap=syscall so the test can make
// io_uring_enter fail for submit-only calls (flush attempts) while leaving
// GETEVENTS waits and every other syscall untouched. This pins:
// - hard flush failures reject only an entry still proven unconsumed;
// - a consumed entry keeps kernel ownership even if the submit call reports
//   an error, so its operation remains registered through the CQE;
// - the poller never submits SQEs outside the context submission lock;
// - the lifecycle wakeup chain self-heals across hard flush failures and
//   across a wakeup NOP that could never be published into a saturated SQ.

#include <forge/io/io_uring_context.hpp>

#include <linux/io_uring.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <execution>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
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

// Errno injected into io_uring_enter calls that only submit (no GETEVENTS
// flag, to_submit > 0). Zero disables injection.
std::atomic<int> inject_submit_errno{0};
// Runs the real submit first, then reports this errno when the kernel call
// returned nonnegative. This emulates a conservative partial-consumption
// outcome and pins the shared-head ownership check.
std::atomic<int> inject_submit_errno_after_success{0};
std::atomic<unsigned> combined_submit_calls{0};

} // namespace

extern "C" long __real_syscall(long number, ...);

// Named-parameter signature for a variadic wrapped function: the SysV
// x86-64 and AArch64 conventions pass the first six integer arguments in
// registers for variadic and non-variadic calls alike, and every
// io_uring_enter call site in the backend passes exactly six arguments.
extern "C" long __wrap_syscall(
    long number,
    long arg1,
    long arg2,
    long arg3,
    long arg4,
    long arg5,
    long arg6) {
    if (number == __NR_io_uring_enter) {
        const long to_submit = arg2;
        const auto flags = static_cast<unsigned>(arg4);
        if (to_submit > 0 && (flags & IORING_ENTER_GETEVENTS) != 0) {
            combined_submit_calls.fetch_add(1, std::memory_order_relaxed);
        }
        const int injected_after_success =
            inject_submit_errno_after_success.load(std::memory_order_acquire);
        if (injected_after_success != 0 && to_submit > 0 &&
            (flags & IORING_ENTER_GETEVENTS) == 0) {
            const long result =
                __real_syscall(number, arg1, arg2, arg3, arg4, arg5, arg6);
            if (result >= 0) {
                errno = injected_after_success;
                return -1;
            }
            return result;
        }
        const int injected =
            inject_submit_errno.load(std::memory_order_acquire);
        if (injected != 0 && to_submit > 0 &&
            (flags & IORING_ENTER_GETEVENTS) == 0) {
            errno = injected;
            return -1;
        }
    }
    return __real_syscall(number, arg1, arg2, arg3, arg4, arg5, arg6);
}

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L

namespace {

namespace cio = forge::io;
using namespace std::chrono_literals;

int failures = 0;

#define FAULT_CHECK(condition)                                               \
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
        if (read_end >= 0) {
            ::close(read_end);
        }
        if (write_end >= 0) {
            ::close(write_end);
        }
    }

    pipe_pair(const pipe_pair&) = delete;
    auto operator=(const pipe_pair&) -> pipe_pair& = delete;
};

auto read_task(
    cio::io_uring_context& context,
    int fd,
    std::span<std::byte> buffer) -> cio::io_task<cio::io_result<std::size_t>> {
    co_return co_await cio::async_read_some(context, fd, buffer);
}

struct completion_state {
    std::mutex mutex;
    std::condition_variable cv;
    std::optional<cio::io_result<std::size_t>> result;
    bool stopped = false;

    [[nodiscard]] auto done() const noexcept -> bool {
        return result.has_value() || stopped;
    }
};

struct completion_receiver {
    using receiver_concept = std::execution::receiver_t;

    std::shared_ptr<completion_state> state;

    auto set_value(cio::io_result<std::size_t> result) && noexcept -> void {
        std::lock_guard lock{state->mutex};
        state->result.emplace(std::move(result));
        state->cv.notify_all();
    }

    auto set_error(std::exception_ptr) && noexcept -> void {
        std::lock_guard lock{state->mutex};
        state->stopped = true;
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

// A hard flush failure on a data SQE must reject the submission with
// no_buffer_space only while the shared head proves the tail entry was not
// consumed. The rollback must leave the ring fully usable.
void check_hard_flush_failure_rejects_submission() {
    cio::io_uring_context context{{.entries = 8}};
    pipe_pair pipe;
    FAULT_CHECK(pipe.read_end >= 0);

    std::byte buffer[8] = {};
    inject_submit_errno.store(ENOMEM);
    auto rejected = std::execution::sync_wait(cio::as_sender(
        read_task(context, pipe.read_end, buffer)));
    inject_submit_errno.store(0);

    FAULT_CHECK(rejected.has_value());
    if (rejected.has_value()) {
        auto [io] = std::move(*rejected);
        FAULT_CHECK(!io.has_value());
        FAULT_CHECK(
            io.error() == std::make_error_code(std::errc::no_buffer_space));
    }
    FAULT_CHECK(
        context.last_flush_diagnostic() ==
        std::error_code(ENOMEM, std::generic_category()));

    // The retracted tail entry must not desynchronize the SQ: a normal
    // operation afterwards completes end to end.
    const char payload = 'x';
    FAULT_CHECK(::write(pipe.write_end, &payload, 1) == 1);
    auto accepted = std::execution::sync_wait(cio::as_sender(
        read_task(context, pipe.read_end, buffer)));
    FAULT_CHECK(accepted.has_value());
    if (accepted.has_value()) {
        auto [io] = std::move(*accepted);
        FAULT_CHECK(io.has_value());
        FAULT_CHECK(std::get<0>(io.values()) == 1);
        FAULT_CHECK(buffer[0] == std::byte{'x'});
    }
    FAULT_CHECK(!context.last_error());
}

// Once the kernel head consumed the SQE, a reported submit error cannot hand
// ownership back to the caller. The operation remains registered and its CQE
// supplies the sole terminal completion.
void check_consumed_submission_retains_kernel_ownership() {
    cio::io_uring_context context{{.entries = 8}};
    pipe_pair pipe;
    FAULT_CHECK(pipe.read_end >= 0);

    const char payload = 'z';
    FAULT_CHECK(::write(pipe.write_end, &payload, 1) == 1);
    std::byte buffer[4] = {};

    inject_submit_errno_after_success.store(ENOMEM);
    auto completed = std::execution::sync_wait(cio::as_sender(
        read_task(context, pipe.read_end, buffer)));
    inject_submit_errno_after_success.store(0);

    FAULT_CHECK(completed.has_value());
    if (completed.has_value()) {
        auto [io] = std::move(*completed);
        FAULT_CHECK(io.has_value());
        FAULT_CHECK(std::get<0>(io.values()) == 1);
        FAULT_CHECK(buffer[0] == std::byte{'z'});
    }
    FAULT_CHECK(!context.last_error());
}

// A lifecycle wakeup NOP whose flush keeps failing hard must not hang
// wait(): the exit pump retries until the kernel accepts it. A successful
// wakeup round trip afterwards clears the flush diagnostic.
void check_wakeup_flush_hard_failure_self_heals() {
    cio::io_uring_context context{{.entries = 8}};
    // Give the poller a moment to block in GETEVENTS so the wakeup NOP is
    // its only way out.
    std::this_thread::sleep_for(10ms);

    inject_submit_errno.store(ENOMEM);
    context.close();
    FAULT_CHECK(
        context.last_flush_diagnostic() ==
        std::error_code(ENOMEM, std::generic_category()));

    std::thread healer{[&] {
        std::this_thread::sleep_for(50ms);
        inject_submit_errno.store(0);
    }};
    context.wait();
    healer.join();

    FAULT_CHECK(!context.last_error());
    FAULT_CHECK(!context.last_flush_diagnostic());
}

// With a single-entry SQ full of a parked (EBUSY) data SQE, the lifecycle
// wakeup NOP cannot even be published. The exit pump must re-drive the
// whole chain: flush the parked entry out, publish the NOP, and drain both
// completions.
void check_saturated_wakeup_publish_self_heals() {
    cio::io_uring_context context{{.entries = 1}};
    pipe_pair pipe;
    FAULT_CHECK(pipe.read_end >= 0);

    // Data is already available, so the parked read completes as soon as
    // the exit pump finally flushes it into the kernel.
    const char payload = 'y';
    FAULT_CHECK(::write(pipe.write_end, &payload, 1) == 1);

    std::byte buffer[4] = {};
    auto state = std::make_shared<completion_state>();
    inject_submit_errno.store(EBUSY);
    auto operation = std::execution::connect(
        cio::as_sender(read_task(context, pipe.read_end, buffer)),
        completion_receiver{state});
    std::execution::start(operation);

    // The single SQ slot now holds the parked read, so the wakeup NOP
    // publish inside close() fails and records the saturation.
    context.close();
    FAULT_CHECK(
        context.last_flush_diagnostic() ==
        std::make_error_code(std::errc::no_buffer_space));

    inject_submit_errno.store(0);
    context.wait();

    FAULT_CHECK(wait_done(state));
    FAULT_CHECK(state->result.has_value());
    if (state->result.has_value()) {
        FAULT_CHECK(state->result->has_value());
        FAULT_CHECK(std::get<0>(state->result->values()) == 1);
        FAULT_CHECK(buffer[0] == std::byte{'y'});
    }
    FAULT_CHECK(!context.last_error());
    FAULT_CHECK(!context.last_flush_diagnostic());
}

} // namespace

int main() {
    try {
        {
            cio::io_uring_context probe{{.entries = 8}};
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

    check_hard_flush_failure_rejects_submission();
    check_consumed_submission_retains_kernel_ownership();
    check_wakeup_flush_hard_failure_self_heals();
    check_saturated_wakeup_publish_self_heals();
    FAULT_CHECK(combined_submit_calls.load(std::memory_order_acquire) == 0);

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
