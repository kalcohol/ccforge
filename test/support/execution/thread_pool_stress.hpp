#pragma once

#include "test_thread_pool.hpp"
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <execution>
#include <latch>
#include <thread>
#include <tuple>

namespace test_execution {

inline void check_standard_pool_composition(const test_thread_pool::scheduler& scheduler) {
    std::atomic<int> when_all_ran{0};
    std::array<std::thread::id, 4> when_all_workers{};
    std::latch children_running{4};
    auto child = [&](int value) {
        return std::execution::schedule(scheduler)
            | std::execution::then([&, value] {
                  when_all_workers[value - 1] = std::this_thread::get_id();
                  // No worker can finish its child before all four enter.
                  children_running.arrive_and_wait();
                  when_all_ran.fetch_add(1, std::memory_order_relaxed);
                  return value;
              });
    };

    auto joined = std::this_thread::sync_wait(
        std::execution::when_all(child(1), child(2), child(3), child(4)));
    ASSERT_TRUE(joined.has_value());
    EXPECT_EQ(std::get<0>(*joined) + std::get<1>(*joined)
                + std::get<2>(*joined) + std::get<3>(*joined), 10);
    EXPECT_EQ(when_all_ran.load(std::memory_order_relaxed), 4);
    for (const auto worker : when_all_workers) {
        EXPECT_NE(worker, std::thread::id{});
        EXPECT_NE(worker, std::this_thread::get_id());
    }

    std::atomic<int> continued{0};
    std::thread::id continued_on;
    auto continued_result = std::this_thread::sync_wait(
        std::execution::continues_on(std::execution::just(5), scheduler)
            | std::execution::then([&](int x) {
                  continued_on = std::this_thread::get_id();
                  continued.fetch_add(1, std::memory_order_relaxed);
                  return x + 1;
              }));
    ASSERT_TRUE(continued_result.has_value());
    EXPECT_EQ(std::get<0>(*continued_result), 6);
    EXPECT_EQ(continued.load(std::memory_order_relaxed), 1);
    EXPECT_TRUE(std::find(when_all_workers.begin(), when_all_workers.end(), continued_on)
                != when_all_workers.end());
}

} // namespace test_execution
