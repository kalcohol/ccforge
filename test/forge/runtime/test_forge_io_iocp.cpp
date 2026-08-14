#include <gtest/gtest.h>

#if !defined(NOMINMAX)
#define FORGE_TEST_EXPECT_NOMINMAX_UNDEFINED
#endif
#if !defined(WIN32_LEAN_AND_MEAN)
#define FORGE_TEST_EXPECT_WIN32_LEAN_AND_MEAN_UNDEFINED
#endif

#include <forge/io.hpp>
#include <forge/io/context_await.hpp>
#include <forge/wait_result.hpp>

#if defined(FORGE_TEST_EXPECT_NOMINMAX_UNDEFINED) && defined(NOMINMAX)
#error "forge IOCP headers must not leak NOMINMAX"
#endif
#if defined(FORGE_TEST_EXPECT_WIN32_LEAN_AND_MEAN_UNDEFINED) && \
    defined(WIN32_LEAN_AND_MEAN)
#error "forge IOCP headers must not leak WIN32_LEAN_AND_MEAN"
#endif

#undef FORGE_TEST_EXPECT_NOMINMAX_UNDEFINED
#undef FORGE_TEST_EXPECT_WIN32_LEAN_AND_MEAN_UNDEFINED

#include "forge_counting_resource.hpp"
#include "forge_operation_destroy.hpp"
#include <execution>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <iterator>
#include <limits>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

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

[[nodiscard]] auto make_pipe_pair(
    DWORD pipe_mode =
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT) -> pipe_pair {
    static std::atomic<unsigned long long> sequence{0};
    auto name = std::wstring{LR"(\\.\pipe\ccforge-iocp-)"} +
        std::to_wstring(::GetCurrentProcessId()) + L"-" +
        std::to_wstring(::GetTickCount64()) + L"-" +
        std::to_wstring(sequence.fetch_add(1, std::memory_order_relaxed));

    unique_handle server{::CreateNamedPipeW(
        name.c_str(),
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        pipe_mode,
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
    int completions = 0;
    std::thread::id completion_thread;

    [[nodiscard]] bool done() const noexcept {
        return value || stopped || error;
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

struct io_receiver {
    using receiver_concept = std::execution::receiver_t;

    std::shared_ptr<io_state> state;

    void set_value(std::size_t bytes) && noexcept {
        {
            std::lock_guard lk{state->mtx};
            state->value = true;
            state->bytes = bytes;
            ++state->completions;
            state->completion_thread = std::this_thread::get_id();
        }
        state->cv.notify_all();
    }

    void set_error(std::exception_ptr error) && noexcept {
        {
            std::lock_guard lk{state->mtx};
            state->error = std::move(error);
            ++state->completions;
            state->completion_thread = std::this_thread::get_id();
        }
        state->cv.notify_all();
    }

    void set_stopped() && noexcept {
        {
            std::lock_guard lk{state->mtx};
            state->stopped = true;
            ++state->completions;
            state->completion_thread = std::this_thread::get_id();
        }
        state->cv.notify_all();
    }

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }
};

struct typed_size_receiver {
    using receiver_concept = std::execution::receiver_t;

    std::shared_ptr<typed_io_state> state;

    void set_value(std::size_t bytes) && noexcept {
        {
            std::lock_guard lk{state->mtx};
            state->value = true;
            state->bytes = bytes;
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

class recycling_memory_resource final : public std::pmr::memory_resource {
public:
    ~recycling_memory_resource() override {
        for (const auto& block : blocks_) {
            upstream_->deallocate(block.ptr, block.bytes, block.alignment);
        }
    }

    [[nodiscard]] auto reuses() const -> std::size_t {
        std::lock_guard lk{mtx_};
        return reuses_;
    }

private:
    struct block {
        void* ptr;
        std::size_t bytes;
        std::size_t alignment;
    };

    void* do_allocate(std::size_t bytes, std::size_t alignment) override {
        std::lock_guard lk{mtx_};
        for (auto it = blocks_.rbegin(); it != blocks_.rend(); ++it) {
            if (it->bytes == bytes && it->alignment == alignment) {
                auto* ptr = it->ptr;
                blocks_.erase(std::next(it).base());
                ++reuses_;
                return ptr;
            }
        }
        return upstream_->allocate(bytes, alignment);
    }

    void do_deallocate(
        void* ptr,
        std::size_t bytes,
        std::size_t alignment) override {
        std::lock_guard lk{mtx_};
        blocks_.push_back(block{ptr, bytes, alignment});
    }

    [[nodiscard]] bool do_is_equal(
        const std::pmr::memory_resource& other) const noexcept override {
        return this == &other;
    }

    std::pmr::memory_resource* upstream_ = std::pmr::new_delete_resource();
    mutable std::mutex mtx_;
    std::vector<block> blocks_;
    std::size_t reuses_ = 0;
};

struct blocking_completion_state {
    std::mutex mtx;
    std::condition_variable cv;
    bool entered = false;
    bool release = false;
};

class blocking_completion_release_guard {
public:
    explicit blocking_completion_release_guard(
        std::shared_ptr<blocking_completion_state> state)
        : state_(std::move(state)) {}

    ~blocking_completion_release_guard() {
        release();
    }

    void release() noexcept {
        if (!state_) {
            return;
        }
        {
            std::lock_guard lk{state_->mtx};
            state_->release = true;
        }
        state_->cv.notify_all();
        state_.reset();
    }

private:
    std::shared_ptr<blocking_completion_state> state_;
};

struct blocking_io_receiver {
    using receiver_concept = std::execution::receiver_t;

    std::shared_ptr<blocking_completion_state> state;

    void set_value(std::size_t) && noexcept { block(); }
    void set_error(std::exception_ptr) && noexcept { block(); }
    void set_stopped() && noexcept { block(); }
    auto get_env() const noexcept -> std::execution::empty_env { return {}; }

private:
    void block() noexcept {
        std::unique_lock lk{state->mtx};
        state->entered = true;
        state->cv.notify_all();
        state->cv.wait(lk, [&] { return state->release; });
    }
};

void write_overlapped(HANDLE handle, std::span<const std::byte> bytes) {
    unique_handle event{::CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    if (!event) {
        throw_last_error("CreateEventW");
    }

    OVERLAPPED overlapped{};
    overlapped.hEvent = event.get();
    const BOOL issued = ::WriteFile(
        handle,
        bytes.data(),
        static_cast<DWORD>(bytes.size()),
        nullptr,
        &overlapped);
    if (issued == FALSE && ::GetLastError() != ERROR_IO_PENDING) {
        throw_last_error("WriteFile");
    }

    DWORD transferred = 0;
    if (::GetOverlappedResult(
            handle, &overlapped, &transferred, TRUE) == FALSE) {
        throw_last_error("GetOverlappedResult");
    }
    if (transferred != bytes.size()) {
        throw std::runtime_error("short overlapped write");
    }
}

struct self_destroying_io_receiver {
    using receiver_concept = std::execution::receiver_t;

    forge_test::destroy_context_base* context = nullptr;

    void set_value(std::size_t) && noexcept { context->destroy(); }
    void set_error(std::exception_ptr) && noexcept { context->destroy(); }
    void set_stopped() && noexcept { context->destroy(); }
    auto get_env() const noexcept -> std::execution::empty_env { return {}; }
};

[[nodiscard]] auto wait_done(const std::shared_ptr<io_state>& state) -> bool {
    std::unique_lock lk{state->mtx};
    return state->cv.wait_for(lk, 2s, [&] { return state->done(); });
}

[[nodiscard]] auto wait_done(const std::shared_ptr<typed_io_state>& state) -> bool {
    std::unique_lock lk{state->mtx};
    return state->cv.wait_for(lk, 2s, [&] { return state->done(); });
}

template<class Op>
class op_slot {
public:
    op_slot() = default;
    ~op_slot() { reset(); }

    op_slot(const op_slot&) = delete;
    auto operator=(const op_slot&) -> op_slot& = delete;
    op_slot(op_slot&&) = delete;
    auto operator=(op_slot&&) -> op_slot& = delete;

    template<class Factory>
    auto emplace_from(Factory&& factory) -> Op& {
        ::new (static_cast<void*>(storage_)) Op(static_cast<Factory&&>(factory)());
        has_value_ = true;
        return get();
    }

    [[nodiscard]] auto get() noexcept -> Op& {
        return *std::launder(reinterpret_cast<Op*>(storage_));
    }

    void reset() noexcept {
        if (!has_value_) {
            return;
        }
        get().~Op();
        has_value_ = false;
    }

private:
    alignas(Op) unsigned char storage_[sizeof(Op)]{};
    bool has_value_ = false;
};

} // namespace

TEST(IoIocpTest, EmptyContextDestroysCleanly) {
    forge::io::context ctx;
    ctx.shutdown();
    ctx.wait();
}

TEST(IoIocpTest, OneShotTransferClampsToWindowsByteCount) {
    constexpr auto dword_max = std::numeric_limits<DWORD>::max();
    EXPECT_EQ(forge::io::__detail::__windows_transfer_size(17), 17u);

    if constexpr (std::numeric_limits<std::size_t>::max() > dword_max) {
        constexpr auto oversized =
            static_cast<std::size_t>(dword_max) + std::size_t{1};
        EXPECT_EQ(
            forge::io::__detail::__windows_transfer_size(oversized),
            dword_max);
    }
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

TEST(IoIocpTest, ContextUsesConfiguredMemoryResource) {
    forge_test::counting_resource memory;
    {
        auto pipe = make_pipe_pair();
        forge::io::context ctx{{.memory = &memory}};

        std::array<std::byte, 3> payload{byte('p'), byte('m'), byte('r')};
        auto write_result = std::execution::sync_wait(
            ctx.async_write_some(
                pipe.client.get(),
                std::span<const std::byte>{payload}));
        ASSERT_TRUE(write_result.has_value());
        EXPECT_EQ(std::get<0>(*write_result), payload.size());

        std::array<std::byte, 3> buffer{};
        auto read_result = std::execution::sync_wait(
            ctx.async_read_some(pipe.server.get(), std::span{buffer}));
        ASSERT_TRUE(read_result.has_value());
        EXPECT_EQ(std::get<0>(*read_result), payload.size());
        EXPECT_EQ(buffer, payload);

        ctx.shutdown();
        ctx.wait();
    }

    EXPECT_GT(memory.allocations(), 0u);
    EXPECT_EQ(memory.outstanding(), 0u);
}

TEST(IoIocpTest, BackendOwnedSkipModeCompletesSynchronousSuccess) {
    auto pipe = make_pipe_pair();
    forge::io::context ctx;
    std::array<std::byte, 3> payload{byte('s'), byte('y'), byte('n')};
    std::array<std::byte, 3> received{};

    auto written = std::execution::sync_wait(
        ctx.async_write_some(pipe.server.get(), std::span{payload}));
    ASSERT_TRUE(written.has_value());
    ASSERT_EQ(std::get<0>(*written), payload.size());

    auto read = std::execution::sync_wait(
        ctx.async_read_some(pipe.client.get(), std::span{received}));
    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(std::get<0>(*read), received.size());
    EXPECT_EQ(received, payload);
}

TEST(IoIocpTest, SynchronousSuccessCannotCompleteARecycledRecord) {
    recycling_memory_resource memory;
    auto blocker_pipe = make_pipe_pair();
    auto target_pipe = make_pipe_pair();
    forge::io::context ctx{{.memory = &memory}};

    auto blocker_state = std::make_shared<blocking_completion_state>();
    blocking_completion_release_guard blocker_release{blocker_state};
    std::array<std::byte, 1> blocker_buffer{};
    auto blocker_op = std::execution::connect(
        ctx.async_read_some(
            blocker_pipe.client.get(), std::span{blocker_buffer}),
        blocking_io_receiver{blocker_state});
    std::execution::start(blocker_op);
    const std::array blocker_payload{byte('b')};
    write_overlapped(blocker_pipe.server.get(), blocker_payload);
    {
        std::unique_lock lk{blocker_state->mtx};
        ASSERT_TRUE(blocker_state->cv.wait_for(
            lk, 2s, [&] { return blocker_state->entered; }));
    }

    const std::array stale_payload{
        byte('o'), byte('l'), byte('d')};
    write_overlapped(target_pipe.server.get(), stale_payload);

    std::array<std::byte, 3> first_buffer{};
    auto first_state = std::make_shared<io_state>();
    using op_t = decltype(std::execution::connect(
        ctx.async_read_some(
            target_pipe.client.get(), std::span{first_buffer}),
        io_receiver{first_state}));
    op_slot<op_t> slot;
    auto& first_op = slot.emplace_from([&] {
        return std::execution::connect(
            ctx.async_read_some(
                target_pipe.client.get(), std::span{first_buffer}),
            io_receiver{first_state});
    });
    std::execution::start(first_op);
    ASSERT_TRUE(wait_done(first_state));
    EXPECT_EQ(first_buffer, stale_payload);
    slot.reset();

    std::array<std::byte, 3> second_buffer{};
    auto second_state = std::make_shared<io_state>();
    auto& second_op = slot.emplace_from([&] {
        return std::execution::connect(
            ctx.async_read_some(
                target_pipe.client.get(), std::span{second_buffer}),
            io_receiver{second_state});
    });
    std::execution::start(second_op);

    blocker_release.release();

    {
        std::unique_lock lk{second_state->mtx};
        EXPECT_FALSE(second_state->cv.wait_for(
            lk, 100ms, [&] { return second_state->done(); }));
    }

    const std::array fresh_payload{
        byte('n'), byte('e'), byte('w')};
    write_overlapped(target_pipe.server.get(), fresh_payload);
    ASSERT_TRUE(wait_done(second_state));
    EXPECT_TRUE(second_state->value);
    EXPECT_EQ(second_state->bytes, fresh_payload.size());
    EXPECT_EQ(second_buffer, fresh_payload);
    EXPECT_EQ(second_state->completions, 1);
}

TEST(IoIocpTest, CompletionPacketFallbackRetainsRecordUntilWorkerDrainsIt) {
    recycling_memory_resource memory;
    auto pipe = make_pipe_pair();
    forge::io::context_options options{.memory = &memory};
    options.force_completion_packets_on_success = true;
    forge::io::context ctx{options};
    const auto caller = std::this_thread::get_id();

    const std::array first_payload{byte('o'), byte('l'), byte('d')};
    write_overlapped(pipe.server.get(), first_payload);

    std::array<std::byte, 3> first_buffer{};
    auto first_state = std::make_shared<io_state>();
    using op_t = decltype(std::execution::connect(
        ctx.async_read_some(pipe.client.get(), std::span{first_buffer}),
        io_receiver{first_state}));
    op_slot<op_t> slot;
    auto& first_op = slot.emplace_from([&] {
        return std::execution::connect(
            ctx.async_read_some(pipe.client.get(), std::span{first_buffer}),
            io_receiver{first_state});
    });
    std::execution::start(first_op);

    ASSERT_TRUE(wait_done(first_state));
    EXPECT_TRUE(first_state->value);
    EXPECT_EQ(first_state->bytes, first_payload.size());
    EXPECT_EQ(first_state->completions, 1);
    EXPECT_NE(first_state->completion_thread, caller);
    EXPECT_EQ(first_buffer, first_payload);
    slot.reset();

    std::array<std::byte, 3> second_buffer{};
    auto second_state = std::make_shared<io_state>();
    auto& second_op = slot.emplace_from([&] {
        return std::execution::connect(
            ctx.async_read_some(pipe.client.get(), std::span{second_buffer}),
            io_receiver{second_state});
    });
    std::execution::start(second_op);
    EXPECT_GT(memory.reuses(), 0u);

    {
        std::unique_lock lk{second_state->mtx};
        EXPECT_FALSE(second_state->cv.wait_for(
            lk, 100ms, [&] { return second_state->done(); }));
    }

    const std::array second_payload{byte('n'), byte('e'), byte('w')};
    write_overlapped(pipe.server.get(), second_payload);
    ASSERT_TRUE(wait_done(second_state));
    EXPECT_TRUE(second_state->value);
    EXPECT_EQ(second_state->bytes, second_payload.size());
    EXPECT_EQ(second_state->completions, 1);
    EXPECT_EQ(second_buffer, second_payload);
}

TEST(IoIocpTest, MessagePipePartialReadCompletesWithTransferredBytes) {
    auto pipe = make_pipe_pair(
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT);
    forge::io::context ctx;
    std::array<std::byte, 64> payload{};
    for (std::size_t i = 0; i < payload.size(); ++i) {
        payload[i] = std::byte{static_cast<unsigned char>(i)};
    }
    write_overlapped(pipe.client.get(), payload);

    std::array<std::byte, 8> buffer{};
    auto result = std::execution::sync_wait(
        ctx.async_read_some(pipe.server.get(), std::span{buffer}));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), buffer.size());
    EXPECT_TRUE(std::equal(buffer.begin(), buffer.end(), payload.begin()));
}

TEST(IoIocpTest, AsyncReadReturnsZeroWhenPeerCloses) {
    auto pipe = make_pipe_pair();
    forge::io::context ctx;

    std::array<std::byte, 1> buffer{};
    auto state = std::make_shared<io_state>();
    auto op = std::execution::connect(
        ctx.async_read_some(pipe.server.get(), std::span{buffer}),
        io_receiver{state});
    std::execution::start(op);

    pipe.client.reset();

    ASSERT_TRUE(wait_done(state));
    EXPECT_TRUE(state->value);
    EXPECT_FALSE(state->stopped);
    EXPECT_FALSE(state->error);
    EXPECT_EQ(state->bytes, 0u);
    EXPECT_EQ(state->completions, 1);
}

TEST(IoIocpTest, AsyncReadReturnsZeroWhenPipeIsDisconnected) {
    auto pipe = make_pipe_pair();
    forge::io::context ctx;
    ASSERT_TRUE(::DisconnectNamedPipe(pipe.server.get()))
        << "DisconnectNamedPipe failed with " << ::GetLastError();

    std::array<std::byte, 1> buffer{};
    auto read_result = std::execution::sync_wait(
        ctx.async_read_some(pipe.server.get(), std::span{buffer}));

    ASSERT_TRUE(read_result.has_value());
    EXPECT_EQ(std::get<0>(*read_result), 0u);
}

TEST(IoIocpTest, ContextAwaitAsyncReadWriteReturnIoResult) {
    auto pipe = make_pipe_pair();
    forge::io::context ctx;

    std::array<std::byte, 3> payload{byte('c'), byte('o'), byte('r')};
    auto write_result = std::execution::sync_wait(
        forge::io::as_sender(
            forge::io::async_write_some(
                ctx,
                pipe.client.get(),
                std::span<const std::byte>{payload})));

    ASSERT_TRUE(write_result.has_value());
    auto [write_io] = std::move(*write_result);
    auto [write_error, written] = write_io;
    EXPECT_FALSE(write_error);
    EXPECT_FALSE(write_io.eof());
    EXPECT_EQ(written, payload.size());

    std::array<std::byte, 3> buffer{};
    auto read_result = std::execution::sync_wait(
        forge::io::as_sender(
            forge::io::async_read_some(ctx, pipe.server.get(), std::span{buffer})));

    ASSERT_TRUE(read_result.has_value());
    auto [read_io] = std::move(*read_result);
    auto [read_error, read_count] = read_io;
    EXPECT_FALSE(read_error);
    EXPECT_FALSE(read_io.eof());
    ASSERT_EQ(read_count, payload.size());
    EXPECT_EQ(buffer, payload);
}

TEST(IoIocpTest, ContextAwaitPeerCloseMapsReadZeroToEof) {
    auto pipe = make_pipe_pair();
    forge::io::context ctx;
    pipe.client.reset();

    std::array<std::byte, 1> buffer{};
    auto read_result = std::execution::sync_wait(
        forge::io::as_sender(
            forge::io::async_read_some(ctx, pipe.server.get(), std::span{buffer})));

    ASSERT_TRUE(read_result.has_value());
    auto [read_io] = std::move(*read_result);
    auto [read_error, read_count] = read_io;
    EXPECT_FALSE(read_error);
    EXPECT_TRUE(read_io.eof());
    EXPECT_EQ(read_count, 0u);
}

TEST(IoIocpTest, ContextAwaitInvalidHandleMapsToIoResultError) {
    forge::io::context ctx;
    std::array<std::byte, 1> buffer{};

    auto read_result = std::execution::sync_wait(
        forge::io::as_sender(
            forge::io::async_read_some(
                ctx,
                INVALID_HANDLE_VALUE,
                std::span{buffer})));

    ASSERT_TRUE(read_result.has_value());
    auto [read_io] = std::move(*read_result);
    auto [read_error, read_count] = read_io;
    EXPECT_TRUE(read_error);
    EXPECT_FALSE(read_io.eof());
    EXPECT_EQ(read_error.category(), std::system_category());
    EXPECT_EQ(read_error.value(), static_cast<int>(ERROR_INVALID_HANDLE));
    EXPECT_EQ(read_count, 0u);
}

TEST(IoIocpTest, AsyncWriteSomeTypedReturnsByteCount) {
    auto pipe = make_pipe_pair();
    forge::io::context ctx;
    std::array<std::byte, 3> payload{byte('t'), byte('y'), byte('p')};
    auto state = std::make_shared<typed_io_state>();

    auto op = std::execution::connect(
        ctx.async_write_some_typed(
            pipe.client.get(),
            std::span<const std::byte>{payload}),
        typed_size_receiver{state});
    std::execution::start(op);

    ASSERT_TRUE(wait_done(state));
    EXPECT_TRUE(state->value);
    EXPECT_FALSE(state->stopped);
    EXPECT_FALSE(state->error);
    EXPECT_EQ(state->bytes, payload.size());

    std::array<std::byte, 3> buffer{};
    auto read_result = std::execution::sync_wait(
        ctx.async_read_some(pipe.server.get(), std::span{buffer}));
    ASSERT_TRUE(read_result.has_value());
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

TEST(IoIocpTest, HandlesHighChurnBorrowedHandles) {
    forge::io::context ctx;

    for (int i = 0; i < 24; ++i) {
        auto pipe = make_pipe_pair();
        std::array<std::byte, 1> payload{
            byte(static_cast<char>('a' + (i % 20)))};
        auto write_result = std::execution::sync_wait(
            ctx.async_write_some(
                pipe.client.get(),
                std::span<const std::byte>{payload}));
        ASSERT_TRUE(write_result.has_value());

        std::array<std::byte, 1> buffer{};
        auto read_result = std::execution::sync_wait(
            ctx.async_read_some(pipe.server.get(), std::span{buffer}));
        ASSERT_TRUE(read_result.has_value());
        EXPECT_EQ(buffer, payload);
    }

    auto pipe = make_pipe_pair();
    std::array<std::byte, 2> payload{byte('o'), byte('k')};
    auto write_result = std::execution::sync_wait(
        ctx.async_write_some(
            pipe.client.get(),
            std::span<const std::byte>{payload}));
    ASSERT_TRUE(write_result.has_value());

    std::array<std::byte, 2> buffer{};
    auto read_result = std::execution::sync_wait(
        ctx.async_read_some(pipe.server.get(), std::span{buffer}));
    ASSERT_TRUE(read_result.has_value());
    EXPECT_EQ(buffer, payload);
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

TEST(IoIocpTest, CancelDrainAllowsLaterOperationOnSameHandle) {
    auto pipe = make_pipe_pair();
    forge::io::context ctx;
    std::array<std::byte, 8> pending_buffer{};
    auto state = std::make_shared<io_state>();

    auto op = std::execution::connect(
        ctx.async_read_some(pipe.server.get(), std::span{pending_buffer}),
        io_receiver{state});
    std::execution::start(op);

    ctx.cancel(pipe.server.get());

    ASSERT_TRUE(wait_done(state));
    EXPECT_FALSE(state->value);
    EXPECT_TRUE(state->stopped);
    EXPECT_FALSE(state->error);

    std::array<std::byte, 8> second_buffer{};
    auto second = std::make_shared<io_state>();
    auto second_op = std::execution::connect(
        ctx.async_read_some(pipe.server.get(), std::span{second_buffer}),
        io_receiver{second});
    std::execution::start(second_op);

    ctx.cancel(pipe.server.get());

    ASSERT_TRUE(wait_done(second));
    EXPECT_FALSE(second->value);
    EXPECT_TRUE(second->stopped);
    EXPECT_FALSE(second->error);
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

TEST(IoIocpStressTest, ConcurrentStopCancelAndShutdownDrainPendingReads) {
    constexpr int kOps = 6;
    constexpr int kIterations = 24;

    using sender_t = decltype(std::declval<forge::io::context&>().async_read_some(
        std::declval<HANDLE>(),
        std::declval<std::span<std::byte>>()));
    using receiver_t = stopped_receiver;
    using op_t = decltype(std::execution::connect(
        std::declval<sender_t>(),
        std::declval<receiver_t>()));

    for (int iteration = 0; iteration < kIterations; ++iteration) {
        forge::io::context ctx;
        std::array<pipe_pair, kOps> pipes;
        std::array<std::array<std::byte, 8>, kOps> buffers{};
        std::array<std::shared_ptr<io_state>, kOps> states;
        std::array<std::inplace_stop_source, kOps> stop_sources;
        std::array<op_slot<op_t>, kOps> ops;

        for (int i = 0; i < kOps; ++i) {
            pipes[i] = make_pipe_pair();
            states[i] = std::make_shared<io_state>();
            auto& op = ops[i].emplace_from([&, i] {
                return std::execution::connect(
                    ctx.async_read_some(
                        pipes[i].server.get(),
                        std::span<std::byte>{buffers[i]}),
                    stopped_receiver{{states[i]}, &stop_sources[i]});
            });
            std::execution::start(op);
        }

        std::atomic<int> ready{0};
        std::atomic<bool> go{false};
        auto wait_for_start = [&] {
            ready.fetch_add(1, std::memory_order_acq_rel);
            while (!go.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
        };

        std::thread token_thread{[&] {
            wait_for_start();
            for (int i = 0; i < kOps; ++i) {
                stop_sources[i].request_stop();
            }
        }};
        std::thread cancel_thread{[&] {
            wait_for_start();
            for (int i = 0; i < kOps; ++i) {
                ctx.cancel(pipes[(i + iteration) % kOps].server.get());
            }
        }};
        std::thread context_thread{[&] {
            wait_for_start();
            ctx.request_stop();
        }};

        while (ready.load(std::memory_order_acquire) != 3) {
            std::this_thread::yield();
        }
        go.store(true, std::memory_order_release);

        token_thread.join();
        cancel_thread.join();
        context_thread.join();

        for (int i = 0; i < kOps; ++i) {
            SCOPED_TRACE(i);
            ASSERT_TRUE(wait_done(states[i]));
            EXPECT_FALSE(states[i]->value);
            EXPECT_TRUE(states[i]->stopped);
            EXPECT_FALSE(states[i]->error);
            EXPECT_EQ(states[i]->completions, 1);
        }

        ctx.shutdown();
        ctx.wait();
    }
}

TEST(IoIocpTest, InvalidHandleCompletesWithError) {
    forge::io::context ctx;
    std::array<std::byte, 1> buffer{};

    EXPECT_THROW(
        (void)std::execution::sync_wait(
            ctx.async_read_some(INVALID_HANDLE_VALUE, std::span{buffer})),
        std::system_error);
}

TEST(IoIocpTest, EmptyTransfersDoNotTouchInvalidHandle) {
    forge::io::context ctx;
    std::span<std::byte> empty_read;
    std::span<const std::byte> empty_write;

    auto read = std::execution::sync_wait(
        ctx.async_read_some(INVALID_HANDLE_VALUE, empty_read));
    auto write = std::execution::sync_wait(
        ctx.async_write_some(INVALID_HANDLE_VALUE, empty_write));

    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(std::get<0>(*read), 0u);
    ASSERT_TRUE(write.has_value());
    EXPECT_EQ(std::get<0>(*write), 0u);
}

TEST(IoIocpTest, TypedInvalidHandleCrossesWaitResult) {
    forge::io::context ctx;
    std::array<std::byte, 1> buffer{};

    auto result = forge::wait_result(
        ctx.async_read_some_typed(INVALID_HANDLE_VALUE, std::span{buffer}));

    ASSERT_TRUE(result.has_error());
    auto* error = result.error_if<forge::io::error>();
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->kind, forge::io::error_kind::invalid_handle);
}

TEST(IoIocpTest, InvalidHandleAllowsReceiverToDestroyOperation) {
    forge::io::context ctx;
    std::array<std::byte, 1> buffer{};

    using sender_t = decltype(ctx.async_read_some(
        INVALID_HANDLE_VALUE,
        std::span{buffer}));
    using receiver_t = self_destroying_io_receiver;
    using op_t = decltype(std::execution::connect(
        std::declval<sender_t>(),
        std::declval<receiver_t>()));

    bool destroyed = false;
    forge_test::operation_destroy_context<op_t> context{&destroyed};

    auto& op = context.emplace_from([&] {
        return std::execution::connect(
            ctx.async_read_some(INVALID_HANDLE_VALUE, std::span{buffer}),
            self_destroying_io_receiver{&context});
    });
    std::execution::start(op);

    EXPECT_TRUE(destroyed);
    EXPECT_FALSE(context.has_value);
}
