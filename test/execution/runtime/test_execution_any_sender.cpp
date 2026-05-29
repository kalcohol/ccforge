#include <gtest/gtest.h>
#include <execution>
#include <forge/any_sender.hpp>
#include <exception>
#include <tuple>
#include <type_traits>
#include <utility>

// forge::any_sender_of tests

using cs_int = std::execution::completion_signatures<
    std::execution::set_value_t(int),
    std::execution::set_error_t(std::exception_ptr),
    std::execution::set_stopped_t()>;

TEST(AnySenderTest, IsASender) {
    static_assert(std::execution::sender<forge::any_sender_of<cs_int>>);
    SUCCEED();
}

TEST(AnySenderTest, DefaultEmpty) {
    forge::any_sender_of<cs_int> s;
    EXPECT_FALSE(bool(s));
}

TEST(AnySenderTest, HoldsJustSender) {
    forge::any_sender_of<cs_int> s = std::execution::just(42);
    EXPECT_TRUE(bool(s));
}

TEST(AnySenderTest, SyncWait) {
    forge::any_sender_of<cs_int> s = std::execution::just(42);
    auto result = s.sync_wait();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 42);
}

TEST(AnySenderTest, MoveSemantics) {
    forge::any_sender_of<cs_int> s1 = std::execution::just(99);
    forge::any_sender_of<cs_int> s2 = std::move(s1);
    EXPECT_FALSE(bool(s1));
    EXPECT_TRUE(bool(s2));
    auto result = s2.sync_wait();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 99);
}
