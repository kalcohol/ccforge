#include <gtest/gtest.h>

#include "hive_model.hpp"

#include <cstdint>
#include <string>

namespace {

TEST(HiveIndependentModel, DeterministicOperationSequence) {
    std::string error;
    EXPECT_TRUE(forge_hive_model_test::run(0x5eed1234u, 450, &error)) << error;
}

TEST(HiveIndependentModel, SeededRandomOperationSequences) {
    constexpr std::uint32_t seeds[] = {
        0u, 1u, 0xdeadbeefu, 0xc001d00du, 0x12345678u};
    for (const auto seed : seeds) {
        std::string error;
        EXPECT_TRUE(forge_hive_model_test::run(seed, 600, &error))
            << "seed=" << seed << ": " << error;
    }
}

} // namespace
