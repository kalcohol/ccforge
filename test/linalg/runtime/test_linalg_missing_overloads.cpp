#include "../extended_cases.hpp"

TEST(LinalgMissingOverloads, IndependentProductsAndMultiplyBackSolves) {
    linalg_conformance::extended_cases(linalg_conformance::sequential_calls{});
}
