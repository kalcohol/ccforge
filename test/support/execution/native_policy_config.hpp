#pragma once

// Include after <execution>, <linalg> or <forge_native_execution.hpp>.
//
// FORGE_TEST_EXPECT_POLICY_OBJECTS and FORGE_TEST_EXPECT_POLICY_UNSEQ are what
// the configure step measured by linking the native names (see
// test/NativePolicyTests.cmake). The header decides again in each translation
// unit. A difference means either a wrong decision in the header or flags that
// change the policy mode and that the configure probes did not see; the test
// suite supports neither (docs/testing.md).
#if !defined(FORGE_TEST_EXPECT_POLICY_OBJECTS) || !defined(FORGE_TEST_EXPECT_POLICY_UNSEQ)
#error "Give this target its policy mode with forge_policy_test_target()"
#endif

#if defined(FORGE_NATIVE_EXECUTION_POLICY_OBJECTS)
static_assert(FORGE_NATIVE_EXECUTION_POLICY_OBJECTS == FORGE_TEST_EXPECT_POLICY_OBJECTS,
    "forge_native_execution.hpp and the configure probe disagree about native execution policy objects");
static_assert(FORGE_NATIVE_EXECUTION_POLICY_UNSEQ == FORGE_TEST_EXPECT_POLICY_UNSEQ,
    "forge_native_execution.hpp and the configure probe disagree about std::execution::unseq");
#endif
