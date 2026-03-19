#include <gtest/gtest.h>

#include "../backport/cpp26/unique_resource.hpp"

namespace {

struct counting_deleter {
    int* calls;

    void operator()(int) const noexcept {
        ++*calls;
    }
};

} // namespace

TEST(UniqueResourceRuntimeTest, BasicUsage) {
    int cleanup_count = 0;
    {
        auto resource = std::make_unique_resource(42, counting_deleter{&cleanup_count});
        EXPECT_EQ(resource.get(), 42);
    }
    EXPECT_EQ(cleanup_count, 1);
}

TEST(UniqueResourceRuntimeTest, ResetRunsDeleterOnce) {
    int cleanup_count = 0;

    auto resource = std::make_unique_resource(42, counting_deleter{&cleanup_count});
    resource.reset();
    resource.reset();

    EXPECT_EQ(cleanup_count, 1);
}

TEST(UniqueResourceRuntimeTest, MoveTransfersOwnership) {
    int cleanup_count = 0;
    {
        auto original = std::make_unique_resource(42, counting_deleter{&cleanup_count});
        auto moved = std::move(original);
        EXPECT_EQ(moved.get(), 42);
    }
    EXPECT_EQ(cleanup_count, 1);
}

TEST(UniqueResourceRuntimeTest, SwapExchangesResourceAndDeleter) {
    int cleanup_count1 = 0;
    int cleanup_count2 = 0;

    {
        auto resource1 = std::make_unique_resource(1, counting_deleter{&cleanup_count1});
        auto resource2 = std::make_unique_resource(2, counting_deleter{&cleanup_count2});

        resource1.swap(resource2);

        EXPECT_EQ(resource1.get(), 2);
        EXPECT_EQ(resource2.get(), 1);
        EXPECT_EQ(resource1.get_deleter().calls, &cleanup_count2);
        EXPECT_EQ(resource2.get_deleter().calls, &cleanup_count1);
    }

    EXPECT_EQ(cleanup_count1, 1);
    EXPECT_EQ(cleanup_count2, 1);
}
