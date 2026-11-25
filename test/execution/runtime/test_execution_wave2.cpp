#include "thread_pool_stress.hpp"
#include <gtest/gtest.h>

TEST(StaticThreadPoolStressTest, WhenAllAndContinuesOnCompleteConcurrently) {
    test_execution::test_thread_pool pool{4};
    auto scheduler = pool.get_scheduler();
    for (int i = 0; i < 64; ++i) {
        ASSERT_NO_FATAL_FAILURE(test_execution::check_standard_pool_composition(scheduler));
    }
}
