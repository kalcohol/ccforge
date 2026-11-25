#include <forge/execution_extensions.hpp>
#include <execution>

namespace ex = std::execution;

int main() {
#if FORGE_REMOVED_NAME == 34 || FORGE_REMOVED_NAME == 35
#if FORGE_REMOVAL_CONTROL
    static_assert(ex::sender<decltype(forge::transfer_just(ex::inline_scheduler{}, 42))>);
#elif FORGE_REMOVED_NAME == 34
    using forge::scope_count;
#else
    using forge::scope_is_closed;
#endif
#else
#error "Select a removed Forge observer"
#endif
}
