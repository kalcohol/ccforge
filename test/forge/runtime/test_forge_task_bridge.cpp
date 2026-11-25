#include <execution>
#include <forge/task.hpp>
#include <gtest/gtest.h>

#include <system_error>

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L
namespace {

forge::task<int> await_error_code_sender() {
    co_await std::execution::just_error(
        std::make_error_code(std::errc::invalid_argument));
    co_return 1;
}

TEST(CoroutineBridgeTest, ErrorCodeCompletionBecomesSystemError) {
    try {
        (void)std::this_thread::sync_wait(await_error_code_sender());
        FAIL() << "Expected a system_error completion";
    } catch (const std::system_error& error) {
        EXPECT_EQ(error.code(), std::make_error_code(std::errc::invalid_argument));
    } catch (...) {
        FAIL() << "Unexpected exception type";
    }
}

forge::task<int> stopped_int_task() {
    co_await std::execution::just_stopped();
    co_return 7;
}

forge::task<void> stopped_void_task() {
    co_await std::execution::just_stopped();
}

TEST(CoroutineBridgeTest, ForgeTaskPropagatesStoppedCompletion) {
    auto int_result = std::this_thread::sync_wait(stopped_int_task());
    EXPECT_FALSE(int_result.has_value());

    auto void_result = std::this_thread::sync_wait(stopped_void_task());
    EXPECT_FALSE(void_result.has_value());
}
} // namespace
#endif
