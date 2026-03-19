#include <gtest/gtest.h>

#include "../backport/cpp26/unique_resource.hpp"

namespace {

struct counting_deleter {
    int* calls;

    void operator()(int) const noexcept {
        ++*calls;
    }
};

struct tracked_handle {
    int value;

    explicit tracked_handle(int initial_value) noexcept
        : value(initial_value) {}

    tracked_handle(const tracked_handle&) = delete;
    tracked_handle& operator=(const tracked_handle&) = delete;

    tracked_handle(tracked_handle&& other) noexcept
        : value(other.value) {
        other.value = -999;
    }

    tracked_handle& operator=(tracked_handle&& other) noexcept {
        value = other.value;
        other.value = -999;
        return *this;
    }
};

bool operator==(const tracked_handle& lhs, const tracked_handle& rhs) noexcept {
    return lhs.value == rhs.value;
}

struct tracked_handle_deleter {
    int* calls;

    void operator()(const tracked_handle&) const noexcept {
        ++*calls;
    }
};

} // namespace

TEST(UniqueResourceCheckedTest, ValidValueRunsDeleterOnScopeExit) {
    int cleanup_count = 0;
    {
        auto resource = std::make_unique_resource_checked(42, -1, counting_deleter{&cleanup_count});
        EXPECT_EQ(resource.get(), 42);
    }
    EXPECT_EQ(cleanup_count, 1);
}

TEST(UniqueResourceCheckedTest, InvalidValueDoesNotRunDeleter) {
    int cleanup_count = 0;
    {
        auto resource = std::make_unique_resource_checked(-1, -1, counting_deleter{&cleanup_count});
        EXPECT_EQ(resource.get(), -1);
    }
    EXPECT_EQ(cleanup_count, 0);
}

TEST(UniqueResourceCheckedTest, InvalidValuePreservesSentinelState) {
    int cleanup_count = 0;

    auto resource = std::make_unique_resource_checked(
        tracked_handle{0},
        tracked_handle{0},
        tracked_handle_deleter{&cleanup_count});

    EXPECT_EQ(resource.get().value, 0);
    EXPECT_EQ(cleanup_count, 0);
}

// T-12: an object created with an invalid value can be reactivated via reset(valid)
TEST(UniqueResourceCheckedTest, ResetReactivatesInvalidResource) {
    int cleanup_count = 0;
    {
        auto resource = std::make_unique_resource_checked(-1, -1, counting_deleter{&cleanup_count});
        // Resource is inactive (invalid == sentinel)
        EXPECT_EQ(cleanup_count, 0);

        // Reactivate with a valid value
        resource.reset(42);
        EXPECT_EQ(resource.get(), 42);
    }
    // Destructor should now clean up the valid value
    EXPECT_EQ(cleanup_count, 1);
}
