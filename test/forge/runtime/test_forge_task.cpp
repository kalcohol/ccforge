#include <gtest/gtest.h>
#include <forge/task.hpp>
#include <execution>
#include <tuple>

#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L

forge::task<int> simple_task() {
    co_return 42;
}

forge::task<void> void_task(int* result) {
    *result = 77;
    co_return;
}

TEST(TaskTest, IntTask) {
    auto t = simple_task();
    auto result = std::execution::sync_wait(std::move(t));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 42);
}

TEST(TaskTest, VoidTask) {
    int val = 0;
    auto t = void_task(&val);
    std::execution::sync_wait(std::move(t));
    EXPECT_EQ(val, 77);
}

#else
TEST(TaskTest, CoroutinesNotAvailable) {
    GTEST_SKIP() << "C++20 coroutines not available on this toolchain";
}
#endif
