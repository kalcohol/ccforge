// Native-name oracle. Its probe does not add the shadow include root, so this
// is the system header. It links one policy object and checks its trait.
#include <execution>
#include <type_traits>

#if defined(FORGE_EXECUTION_BACKPORT_ACTIVE) || defined(FORGE_NATIVE_EXECUTION_POLICY_OBJECTS)
#error The oracle must use the system execution header directly
#endif

template<class Policy>
bool links(const Policy& policy) {
    static_assert(std::is_execution_policy_v<Policy>);
#ifdef FORGE_TEST_NATIVE_POLICY_COPYABLE
    static_assert(std::is_constructible_v<Policy, const Policy&>);
#endif
    const Policy* volatile address = &policy;
    return address != nullptr;
}

int main() {
    return !links(std::execution::FORGE_TEST_NATIVE_POLICY_NAME);
}
