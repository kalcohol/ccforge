#include <forge/execution_extensions.hpp>
#include <type_traits>

// A complete native execution implementation must not need backport internals.
#if defined(FORGE_EXECUTION_BACKPORT_ACTIVE)
#error "This probe must use native execution"
#endif
static_assert(std::same_as<forge::empty_env, std::execution::env<>>);
using source = decltype(std::execution::just(42));
static_assert(std::same_as<decltype(forge::sync_wait(std::declval<source>())),
    decltype(std::this_thread::sync_wait(std::declval<source>()))>);
template<class Scheduler>
void check_transfer(Scheduler scheduler) {
    auto sender = forge::transfer_just(scheduler, 42);
    static_assert(std::execution::sender<decltype(sender)>);
}
int main() {
    std::execution::run_loop loop;
    check_transfer(loop.get_scheduler());
}
