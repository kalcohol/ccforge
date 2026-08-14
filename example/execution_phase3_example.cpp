#include <execution>
#include "example_support.hpp"
#include <tuple>

namespace ex = std::execution;

int main() {
    // when_all: run two senders, combine results
    auto result = std::this_thread::sync_wait(
        ex::when_all(ex::just(42), ex::just(3.14)));
    forge_example::require(result.has_value());
    auto [a, b] = *result;
    forge_example::require(a == 42);
    forge_example::require(b > 3.0);

    // split: reuse a single sender result
    auto shared = ex::split(ex::just(100) | ex::then([](int x) { return x * 2; }));
    auto first_shared = std::this_thread::sync_wait(shared);
    auto second_shared = std::this_thread::sync_wait(shared);
    forge_example::require(first_shared.has_value());
    forge_example::require(second_shared.has_value());
    forge_example::require(std::get<0>(*first_shared) == 200);
    forge_example::require(std::get<0>(*second_shared) == 200);

    // bulk: serial iteration
    int sum = 0;
    auto bulk_result = std::this_thread::sync_wait(
        ex::just(0) | ex::bulk(5, [&sum](int i, int&) { sum += i; }));
    forge_example::require(bulk_result.has_value());
    forge_example::require(sum == 0+1+2+3+4);

    // spawn: fire and forget under a scope token
    ex::simple_counting_scope scope;
    ex::spawn(ex::just(), scope.get_token());
    auto join_result = std::this_thread::sync_wait(scope.join());
    forge_example::require(join_result.has_value());

    return 0;
}
