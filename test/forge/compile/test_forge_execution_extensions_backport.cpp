#include <forge/execution_extensions.hpp>

#include <concepts>
#include <execution>

namespace ex = std::execution;

static_assert(&forge::sync_wait == &std::this_thread::sync_wait);
static_assert(std::same_as<forge::empty_env, ex::env<>>);
static_assert(ex::sender<decltype(forge::split(ex::just(42)))>);

int main() {}
