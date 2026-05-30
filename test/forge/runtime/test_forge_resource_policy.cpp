#include <gtest/gtest.h>
#include <forge/resource_policy.hpp>
#include "forge_counting_resource.hpp"
#include <memory_resource>
#include <vector>

TEST(ForgeResourcePolicyTest, DefaultMemoryResourceMatchesPmrDefault) {
    EXPECT_EQ(forge::default_memory_resource(), std::pmr::get_default_resource());

    forge::resource_policy policy;
    EXPECT_EQ(policy.memory, std::pmr::get_default_resource());
}

TEST(ForgeResourcePolicyTest, NullMemoryResourceNormalizesToDefault) {
    EXPECT_EQ(
        forge::normalize_memory_resource(nullptr),
        std::pmr::get_default_resource());
}

TEST(ForgeResourcePolicyTest, CustomMemoryResourceIsObservable) {
    forge_test::counting_resource resource;

    {
        std::pmr::vector<int> values{forge::normalize_memory_resource(&resource)};
        values.reserve(16);
        values.push_back(42);
        EXPECT_EQ(values.front(), 42);
        EXPECT_GT(resource.allocations(), 0u);
    }

    EXPECT_EQ(resource.allocations(), resource.deallocations());
}
