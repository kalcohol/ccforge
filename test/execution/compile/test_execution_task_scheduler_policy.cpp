#include <execution>

#include <type_traits>

#if !FORGE_NATIVE_EXECUTION_POLICY_OBJECTS
#error Native execution policy objects are unavailable
#endif

static_assert(std::is_execution_policy_v<std::remove_cvref_t<decltype(std::execution::par)>>);

int main() {}
