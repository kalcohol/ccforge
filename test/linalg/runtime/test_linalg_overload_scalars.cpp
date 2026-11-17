#include "../conformance_cases.hpp"

TEST(LinalgOverloadScalars, EveryDraftTwinHasAnIndependentExpectation) {
    linalg_conformance::scalar_cases(linalg_conformance::sequential_calls{});
}
