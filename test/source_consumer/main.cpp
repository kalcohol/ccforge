#include <forge/execution.hpp>

#include <execution>
#include <tuple>

int main() {
    auto result = std::execution::sync_wait(
        std::execution::just(20)
        | std::execution::then([](int value) { return value + 22; }));
    return result && std::get<0>(*result) == 42 ? 0 : 1;
}
