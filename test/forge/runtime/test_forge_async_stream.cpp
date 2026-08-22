#include <gtest/gtest.h>

#include <forge/io/async_stream.hpp>
#include <forge/io/memory_stream.hpp>

#include "forge_counting_resource.hpp"

#include <array>
#include <cstddef>
#include <exception>
#include <execution>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <utility>

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L

namespace {

namespace cio = forge::io;

class test_result_awaitable {
public:
    explicit test_result_awaitable(
        cio::io_result<std::size_t> result = {}) noexcept
        : result_(std::move(result))
    {}

    [[nodiscard]] auto await_ready() const noexcept -> bool {
        return true;
    }

    auto await_suspend(
        std::coroutine_handle<>,
        const cio::io_env*) const noexcept -> bool {
        return false;
    }

    [[nodiscard]] auto await_resume() -> cio::io_result<std::size_t> {
        return std::move(result_);
    }

private:
    cio::io_result<std::size_t> result_;
};

class wrong_result_awaitable {
public:
    [[nodiscard]] auto await_ready() const noexcept -> bool {
        return true;
    }

    auto await_suspend(
        std::coroutine_handle<>,
        const cio::io_env*) const noexcept -> bool {
        return false;
    }

    [[nodiscard]] auto await_resume() const noexcept -> int {
        return 0;
    }
};

struct wrong_result_async_read_stream {
    [[nodiscard]] auto read_some(cio::mutable_buffer)
        -> wrong_result_awaitable {
        return {};
    }
};

struct reference_async_read_stream {
    [[nodiscard]] auto read_some(cio::mutable_buffer)
        -> test_result_awaitable& {
        return awaitable;
    }

    test_result_awaitable awaitable;
};

struct const_buffer_async_read_stream {
    [[nodiscard]] auto read_some(cio::const_buffer)
        -> test_result_awaitable {
        return test_result_awaitable{};
    }
};

struct mutable_buffer_async_write_stream {
    [[nodiscard]] auto write_some(cio::mutable_buffer)
        -> test_result_awaitable {
        return test_result_awaitable{};
    }
};

struct manual_read_state {
    cio::__coro_detail::resume_target continuation{};
    cio::mutable_buffer output{};
    std::optional<cio::io_result<std::size_t>> result;
    bool waiting = false;
    bool has_resume_credit = false;

    auto complete_value(std::string_view input) -> void {
        const auto count = cio::buffer_copy(
            output,
            cio::const_buffer{input.data(), input.size()});
        result.emplace(cio::io_result<std::size_t>::success(count));
        waiting = false;
        auto pending = std::exchange(continuation, {});
        pending.resume();
    }
};

class manual_read_awaitable {
public:
    manual_read_awaitable(
        std::shared_ptr<manual_read_state> state,
        cio::mutable_buffer output) noexcept
        : state_(std::move(state))
        , output_(output)
    {}

    [[nodiscard]] auto await_ready() const noexcept -> bool {
        return false;
    }

    auto await_suspend(
        std::coroutine_handle<> continuation,
        const cio::io_env*) noexcept -> bool {
        return publish(cio::__coro_detail::resume_target{continuation, nullptr});
    }

    auto await_suspend(
        cio::__coro_detail::resume_target continuation,
        const cio::io_env*) noexcept -> bool {
        return publish(continuation);
    }

    auto publish(cio::__coro_detail::resume_target continuation) noexcept
        -> bool {
        state_->continuation = continuation;
        state_->has_resume_credit = continuation.root != nullptr;
        state_->output = output_;
        state_->waiting = true;
        return true;
    }

    [[nodiscard]] auto await_resume() -> cio::io_result<std::size_t> {
        auto result = std::move(*state_->result);
        state_->result.reset();
        return result;
    }

private:
    std::shared_ptr<manual_read_state> state_;
    cio::mutable_buffer output_;
};

class manual_async_read_stream {
public:
    explicit manual_async_read_stream(
        std::shared_ptr<manual_read_state> state) noexcept
        : state_(std::move(state))
    {}

    [[nodiscard]] auto read_some(cio::mutable_buffer output)
        -> manual_read_awaitable {
        return manual_read_awaitable{state_, output};
    }

private:
    std::shared_ptr<manual_read_state> state_;
};

class throwing_resume_awaitable {
public:
    [[nodiscard]] auto await_ready() const noexcept -> bool {
        return true;
    }

    auto await_suspend(
        std::coroutine_handle<>,
        const cio::io_env*) const noexcept -> bool {
        return false;
    }

    [[nodiscard]] auto await_resume() -> cio::io_result<std::size_t> {
        throw std::runtime_error{"read completion failed"};
    }
};

struct throwing_async_read_stream {
    [[nodiscard]] auto read_some(cio::mutable_buffer)
        -> throwing_resume_awaitable {
        return {};
    }
};

class tracked_sync_read_stream {
public:
    tracked_sync_read_stream(
        std::string_view input,
        int& destructions) noexcept
        : stream_(input)
        , destructions_(&destructions)
    {}

    tracked_sync_read_stream(const tracked_sync_read_stream&) = delete;
    auto operator=(const tracked_sync_read_stream&)
        -> tracked_sync_read_stream& = delete;

    tracked_sync_read_stream(tracked_sync_read_stream&& other) noexcept
        : stream_(other.stream_)
        , destructions_(std::exchange(other.destructions_, nullptr))
    {}

    auto operator=(tracked_sync_read_stream&&)
        -> tracked_sync_read_stream& = delete;

    ~tracked_sync_read_stream() {
        if (destructions_ != nullptr) {
            ++*destructions_;
        }
    }

    [[nodiscard]] auto read_some(cio::mutable_buffer output) noexcept
        -> cio::io_result<std::size_t> {
        return stream_.read_some(output);
    }

private:
    cio::memory_read_stream stream_;
    int* destructions_;
};

struct completion_state {
    std::optional<cio::io_result<std::size_t>> result;
    std::exception_ptr error;
    bool stopped = false;
};

struct completion_receiver {
    using receiver_concept = std::execution::receiver_t;

    completion_state* state;

    auto set_value(cio::io_result<std::size_t> result) && noexcept -> void {
        state->result.emplace(std::move(result));
    }

    auto set_error(std::exception_ptr error) && noexcept -> void {
        state->error = std::move(error);
    }

    auto set_stopped() && noexcept -> void {
        state->stopped = true;
    }

    [[nodiscard]] auto get_env() const noexcept
        -> std::execution::empty_env {
        return {};
    }
};

template<cio::async_read_stream Stream>
auto read_once(Stream& stream, cio::mutable_buffer output)
    -> cio::io_task<cio::io_result<std::size_t>> {
    co_return co_await stream.read_some(output);
}

template<cio::async_write_stream Stream>
auto write_once(Stream& stream, cio::const_buffer input)
    -> cio::io_task<cio::io_result<std::size_t>> {
    co_return co_await stream.write_some(input);
}

} // namespace

static_assert(cio::async_read_stream<manual_async_read_stream>);
static_assert(!cio::async_read_stream<cio::memory_read_stream>);
static_assert(!cio::async_read_stream<wrong_result_async_read_stream>);
static_assert(!cio::async_read_stream<reference_async_read_stream>);
static_assert(!cio::async_read_stream<const_buffer_async_read_stream>);
static_assert(!cio::async_write_stream<mutable_buffer_async_write_stream>);
static_assert(cio::async_read_stream<
    cio::immediate_async_stream<cio::memory_read_stream>>);
static_assert(cio::async_write_stream<
    cio::immediate_async_stream<cio::memory_write_stream>>);
static_assert(std::is_move_constructible_v<
    cio::owning_any_async_read_stream>);
static_assert(!std::is_nothrow_move_constructible_v<
    cio::owning_any_async_read_stream>);
static_assert(!std::is_copy_constructible_v<
    cio::owning_any_async_read_stream>);

TEST(ForgeAsyncStreamTest, ImmediateAdapterTransfersReadAndWriteData) {
    std::string input{"forge"};
    cio::owning_any_async_read_stream reader{
        cio::immediate_async_stream{
            cio::memory_read_stream{std::string_view{input}}}};
    std::array<char, 5> read_output{};

    auto read_completion = std::execution::sync_wait(
        cio::as_sender(read_once(
            reader,
            cio::mutable_buffer{std::span{read_output}})));

    ASSERT_TRUE(read_completion.has_value());
    auto [read_result] = std::move(*read_completion);
    EXPECT_TRUE(read_result.has_value());
    EXPECT_EQ(cio::get<1>(read_result), read_output.size());
    EXPECT_EQ(
        std::string_view(read_output.data(), read_output.size()),
        "forge");

    std::array<char, 4> write_output{};
    cio::owning_any_async_write_stream writer{
        cio::immediate_async_stream{
            cio::memory_write_stream{
                cio::mutable_buffer{std::span{write_output}}}}};

    auto write_completion = std::execution::sync_wait(
        cio::as_sender(write_once(
            writer,
            cio::const_buffer{"byte", 4})));

    ASSERT_TRUE(write_completion.has_value());
    auto [write_result] = std::move(*write_completion);
    EXPECT_TRUE(write_result.has_value());
    EXPECT_EQ(cio::get<1>(write_result), write_output.size());
    EXPECT_EQ(
        std::string_view(write_output.data(), write_output.size()),
        "byte");
}

TEST(ForgeAsyncStreamTest, ErasedReadTrulySuspendsAndResumesCoroutine) {
    auto manual = std::make_shared<manual_read_state>();
    cio::owning_any_async_read_stream reader{
        manual_async_read_stream{manual}};
    std::array<char, 5> output{};
    completion_state completion;
    auto sender = cio::as_sender(read_once(
        reader,
        cio::mutable_buffer{std::span{output}}));
    auto operation = std::execution::connect(
        std::move(sender),
        completion_receiver{&completion});

    std::execution::start(operation);

    EXPECT_TRUE(manual->waiting);
    EXPECT_TRUE(manual->has_resume_credit);
    EXPECT_FALSE(completion.result.has_value());
    manual->complete_value("async");

    ASSERT_TRUE(completion.result.has_value());
    EXPECT_FALSE(completion.error);
    EXPECT_FALSE(completion.stopped);
    EXPECT_TRUE(completion.result->has_value());
    EXPECT_EQ(cio::get<1>(*completion.result), output.size());
    EXPECT_EQ(std::string_view(output.data(), output.size()), "async");
}

namespace {

[[maybe_unused]] void abandon_suspended_erased_read() {
    auto manual = std::make_shared<manual_read_state>();
    // Leaked on purpose: the stream must stay alive so only the erased
    // awaitable's abandonment guard can end the process, not the stream
    // destructor's active-slot tripwire.
    auto* reader = new cio::owning_any_async_read_stream{
        manual_async_read_stream{manual}};
    static std::array<char, 5> output{};
    static completion_state completion;
    {
        auto operation = std::execution::connect(
            cio::as_sender(read_once(
                *reader,
                cio::mutable_buffer{std::span{output}})),
            completion_receiver{&completion});
        std::execution::start(operation);
        // Suspended on the manual stream: leaving this scope destroys the
        // coroutine frame while the erased operation is started and
        // unresumed, which must terminate instead of leaving a dangling
        // continuation in the slot.
    }
}

} // namespace

TEST(ForgeAsyncStreamDeathTest, AbandoningSuspendedErasedReadTerminates) {
#if GTEST_HAS_DEATH_TEST
    EXPECT_DEATH(abandon_suspended_erased_read(), "");
#else
    GTEST_SKIP() << "death tests are not supported by this gtest build";
#endif
}

TEST(ForgeAsyncStreamTest, CompoundErrorAndEofRemainDistinct) {
    cio::owning_any_async_read_stream reader{
        cio::immediate_async_stream{
            cio::scripted_read_stream{
                cio::scripted_read_step::error(
                    std::make_error_code(std::errc::connection_reset)),
                cio::scripted_read_step::eof()}}};
    std::array<char, 1> output{};

    auto error_completion = std::execution::sync_wait(
        cio::as_sender(read_once(
            reader,
            cio::mutable_buffer{std::span{output}})));
    ASSERT_TRUE(error_completion.has_value());
    auto [error_result] = std::move(*error_completion);
    EXPECT_EQ(
        error_result.error(),
        std::make_error_code(std::errc::connection_reset));
    EXPECT_FALSE(error_result.eof());

    auto eof_completion = std::execution::sync_wait(
        cio::as_sender(read_once(
            reader,
            cio::mutable_buffer{std::span{output}})));
    ASSERT_TRUE(eof_completion.has_value());
    auto [eof_result] = std::move(*eof_completion);
    EXPECT_FALSE(eof_result.error());
    EXPECT_TRUE(eof_result.eof());
    EXPECT_EQ(cio::get<1>(eof_result), 0u);
}

TEST(ForgeAsyncStreamTest, SingleFlightRejectsOverlapAndActiveMutation) {
    std::string input{"abc"};
    cio::owning_any_async_read_stream reader{
        cio::immediate_async_stream{
            cio::memory_read_stream{std::string_view{input}}}};
    std::array<char, 1> output{};

    {
        auto first = reader.read_some(
            cio::mutable_buffer{std::span{output}});
        auto overlapping = reader.read_some(
            cio::mutable_buffer{std::span{output}});

        ASSERT_TRUE(overlapping.await_ready());
        auto overlap_result = overlapping.await_resume();
        EXPECT_EQ(
            overlap_result.error(),
            std::make_error_code(std::errc::operation_in_progress));
        EXPECT_THROW(reader.reset(), std::logic_error);
        EXPECT_THROW(
            (void)cio::owning_any_async_read_stream{std::move(reader)},
            std::logic_error);
    }

    cio::owning_any_async_read_stream moved{std::move(reader)};
    EXPECT_FALSE(reader);
    EXPECT_TRUE(moved);

    auto completion = std::execution::sync_wait(
        cio::as_sender(read_once(
            moved,
            cio::mutable_buffer{std::span{output}})));
    ASSERT_TRUE(completion.has_value());
    auto [result] = std::move(*completion);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(cio::get<1>(result), 1u);
    EXPECT_EQ(output[0], 'b');
}

TEST(ForgeAsyncStreamTest, EmptyErasedStreamsReturnBadAddress) {
    cio::owning_any_async_read_stream reader;
    cio::owning_any_async_write_stream writer;
    std::array<char, 1> output{};

    auto read_operation = reader.read_some(
        cio::mutable_buffer{std::span{output}});
    auto write_operation = writer.write_some(
        cio::const_buffer{"x", 1});

    ASSERT_TRUE(read_operation.await_ready());
    ASSERT_TRUE(write_operation.await_ready());
    auto read_result = read_operation.await_resume();
    auto write_result = write_operation.await_resume();

    EXPECT_EQ(
        read_result.error(),
        std::make_error_code(std::errc::bad_address));
    EXPECT_EQ(
        write_result.error(),
        std::make_error_code(std::errc::bad_address));
}

TEST(ForgeAsyncStreamTest, AwaitResumeExceptionReleasesOperationSlot) {
    cio::owning_any_async_read_stream reader{
        throwing_async_read_stream{}};
    std::array<char, 1> output{};

    EXPECT_THROW(
        (void)std::execution::sync_wait(
            cio::as_sender(read_once(
                reader,
                cio::mutable_buffer{std::span{output}}))),
        std::runtime_error);
    EXPECT_NO_THROW(reader.reset());
    EXPECT_FALSE(reader);
}

TEST(ForgeAsyncStreamTest, OperationErasurePerformsNoPmrAllocation) {
    forge_test::counting_resource memory;
    std::string input{"x"};
    std::array<char, 1> output{};

    {
        cio::owning_any_async_read_stream reader{
            cio::immediate_async_stream{
                cio::memory_read_stream{std::string_view{input}}},
            &memory};
        ASSERT_EQ(memory.allocations(), 1u);

        for (int index = 0; index < 32; ++index) {
            auto operation = reader.read_some(
                cio::mutable_buffer{std::span{output}});
            ASSERT_TRUE(operation.await_ready());
            (void)operation.await_resume();
        }

        EXPECT_EQ(memory.allocations(), 1u);
        EXPECT_EQ(memory.deallocations(), 0u);
    }

    EXPECT_EQ(memory.allocations(), 1u);
    EXPECT_EQ(memory.deallocations(), 1u);
    EXPECT_EQ(memory.outstanding(), 0u);

    std::array<char, 32> write_output{};
    {
        cio::owning_any_async_write_stream writer{
            cio::immediate_async_stream{
                cio::memory_write_stream{
                    cio::mutable_buffer{std::span{write_output}}}},
            &memory};
        ASSERT_EQ(memory.allocations(), 2u);

        for (int index = 0; index < 32; ++index) {
            auto operation = writer.write_some(
                cio::const_buffer{"x", 1});
            ASSERT_TRUE(operation.await_ready());
            (void)operation.await_resume();
        }

        EXPECT_EQ(memory.allocations(), 2u);
        EXPECT_EQ(memory.deallocations(), 1u);
    }

    EXPECT_EQ(memory.allocations(), 2u);
    EXPECT_EQ(memory.deallocations(), 2u);
    EXPECT_EQ(memory.outstanding(), 0u);
}

TEST(ForgeAsyncStreamTest, OwningWrapperDestroysTargetExactlyOnce) {
    int destructions = 0;
    {
        cio::owning_any_async_read_stream reader{
            cio::immediate_async_stream{
                tracked_sync_read_stream{"x", destructions}}};
        EXPECT_TRUE(reader);
        EXPECT_EQ(destructions, 0);
        reader.reset();
        EXPECT_FALSE(reader);
        EXPECT_EQ(destructions, 1);
    }
    EXPECT_EQ(destructions, 1);
}

#else

TEST(ForgeAsyncStreamTest, CoroutinesNotAvailable) {
    GTEST_SKIP() << "C++20 coroutines not available on this toolchain";
}

#endif
