#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "../backport/cpp26/unique_resource.hpp"

namespace {

struct counting_deleter {
    int* calls;

    void operator()(int) const noexcept {
        ++*calls;
    }
};

struct recording_deleter {
    std::vector<int>* cleaned;

    void operator()(int v) const noexcept {
        cleaned->push_back(v);
    }
};

struct simple_object {
    int x;
    int y;
};

struct pointer_deleter {
    int* calls;

    void operator()(simple_object* p) const noexcept {
        ++*calls;
        delete p;
    }
};

} // namespace

TEST(UniqueResourceRuntimeTest, BasicUsage) {
    int cleanup_count = 0;
    {
        std::unique_resource resource(42, counting_deleter{&cleanup_count});
        EXPECT_EQ(resource.get(), 42);
    }
    EXPECT_EQ(cleanup_count, 1);
}

TEST(UniqueResourceRuntimeTest, ResetRunsDeleterOnce) {
    int cleanup_count = 0;

    std::unique_resource resource(42, counting_deleter{&cleanup_count});
    resource.reset();
    resource.reset();

    EXPECT_EQ(cleanup_count, 1);
}

TEST(UniqueResourceRuntimeTest, MoveTransfersOwnership) {
    int cleanup_count = 0;
    {
        std::unique_resource original(42, counting_deleter{&cleanup_count});
        auto moved = std::move(original);
        EXPECT_EQ(moved.get(), 42);
    }
    EXPECT_EQ(cleanup_count, 1);
}

TEST(UniqueResourceRuntimeTest, SwapExchangesResourceAndDeleter) {
    int cleanup_count1 = 0;
    int cleanup_count2 = 0;

    {
        std::unique_resource resource1(1, counting_deleter{&cleanup_count1});
        std::unique_resource resource2(2, counting_deleter{&cleanup_count2});

        resource1.swap(resource2);

        EXPECT_EQ(resource1.get(), 2);
        EXPECT_EQ(resource2.get(), 1);
        EXPECT_EQ(resource1.get_deleter().calls, &cleanup_count2);
        EXPECT_EQ(resource2.get_deleter().calls, &cleanup_count1);
    }

    EXPECT_EQ(cleanup_count1, 1);
    EXPECT_EQ(cleanup_count2, 1);
}

// T-1: release() disables cleanup on destruction
TEST(UniqueResourceRuntimeTest, ReleaseDisablesCleanup) {
    int cleanup_count = 0;
    {
        std::unique_resource resource(42, counting_deleter{&cleanup_count});
        resource.release();
    }
    EXPECT_EQ(cleanup_count, 0);
}

// T-2: move assignment transfers ownership and cleans up the target
TEST(UniqueResourceRuntimeTest, MoveAssignmentTransfersOwnership) {
    int cleanup_count1 = 0;
    int cleanup_count2 = 0;
    {
        std::unique_resource resource1(1, counting_deleter{&cleanup_count1});
        std::unique_resource resource2(2, counting_deleter{&cleanup_count2});
        resource2 = std::move(resource1);
        EXPECT_EQ(resource2.get(), 1);
    }
    EXPECT_EQ(cleanup_count1, 1);
    EXPECT_EQ(cleanup_count2, 1);
}

// T-3: reset(R) cleans old value and holds new value
TEST(UniqueResourceRuntimeTest, ResetWithNewValueCleansOldAndHoldsNew) {
    std::vector<int> cleaned_values;
    {
        std::unique_resource resource(10, recording_deleter{&cleaned_values});
        resource.reset(20);
        // Old value 10 should have been cleaned
        ASSERT_EQ(cleaned_values.size(), 1u);
        EXPECT_EQ(cleaned_values[0], 10);
        EXPECT_EQ(resource.get(), 20);
    }
    // Destructor cleans new value 20
    ASSERT_EQ(cleaned_values.size(), 2u);
    EXPECT_EQ(cleaned_values[1], 20);
}

// T-4: operator* and operator-> for pointer-like resources
TEST(UniqueResourceRuntimeTest, DereferenceAndArrowOperators) {
    int cleanup_count = 0;
    {
        std::unique_resource resource(
            new simple_object{3, 7}, pointer_deleter{&cleanup_count});
        EXPECT_EQ((*resource).x, 3);
        EXPECT_EQ(resource->y, 7);
    }
    EXPECT_EQ(cleanup_count, 1);
}

// T-8: ADL swap works via using-declaration
TEST(UniqueResourceRuntimeTest, AdlSwapWorks) {
    int cleanup_count1 = 0;
    int cleanup_count2 = 0;

    {
        std::unique_resource r1(1, counting_deleter{&cleanup_count1});
        std::unique_resource r2(2, counting_deleter{&cleanup_count2});

        using std::swap;
        swap(r1, r2);

        EXPECT_EQ(r1.get(), 2);
        EXPECT_EQ(r2.get(), 1);
        EXPECT_EQ(r1.get_deleter().calls, &cleanup_count2);
        EXPECT_EQ(r2.get_deleter().calls, &cleanup_count1);
    }

    EXPECT_EQ(cleanup_count1, 1);
    EXPECT_EQ(cleanup_count2, 1);
}
