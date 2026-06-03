#include <execution>

#include <cassert>
#include <iostream>
#include <tuple>
#include <utility>

namespace ex = std::execution;

int main() {
    ex::simple_counting_scope scope;
    auto token = scope.get_token();

    ex::spawn(ex::just(), token);

    auto future = ex::spawn_future(
        ex::just(21) | ex::then([](int value) noexcept {
            return value * 2;
        }),
        token);

    auto result = ex::sync_wait(std::move(future));
    assert(result.has_value());
    assert(std::get<0>(*result) == 42);

    scope.close();
    ex::sync_wait(scope.join());

    std::cout << "spawn_future=" << std::get<0>(*result) << '\n';
    return 0;
}
