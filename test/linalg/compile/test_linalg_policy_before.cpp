#include <execution>
#include "execution_policy_probe.hpp"
int linalg_policy_other_tu();
int main() { return linalg_policy_other_tu() != FORGE_LINALG_HAS_EXECUTION_POLICY_OVERLOADS; }
