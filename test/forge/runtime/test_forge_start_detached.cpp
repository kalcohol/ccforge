#include <gtest/gtest.h>
#include <forge/start_detached.hpp>

#include <execution>

TEST(StartDetachedTest, ErrorCompletionTerminates) {
#if GTEST_HAS_DEATH_TEST
    EXPECT_DEATH(
        {
            forge::start_detached(std::execution::just_error(17));
        },
        "");
#else
    GTEST_SKIP() << "death tests are not supported by this gtest build";
#endif
}
