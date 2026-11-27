#include <execution>

namespace ex = std::execution;

int main() {
    auto fn = [](auto...) noexcept {};
#ifdef FORGE_TEST_BULK_REJECT_PIPE
    (void)(ex::just() | ex::FORGE_TEST_BULK_CPO(ex::par, 4, fn));
#else
    (void)ex::FORGE_TEST_BULK_CPO(ex::just(), ex::par, 4, fn);
#endif
}
