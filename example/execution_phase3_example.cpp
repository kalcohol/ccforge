#include <execution>
#include <cassert>
#include <tuple>

namespace ex = std::execution;

int main() {
    // when_all: run two senders, combine results
    auto result = ex::sync_wait(ex::when_all(ex::just(42), ex::just(3.14)));
    assert(result.has_value());
    auto [a, b] = *result;
    assert(a == 42);
    assert(b > 3.0);

    // split: reuse a single sender result
    auto shared = ex::split(ex::just(100) | ex::then([](int x) { return x * 2; }));
    assert(std::get<0>(*ex::sync_wait(shared)) == 200);
    assert(std::get<0>(*ex::sync_wait(shared)) == 200);

    // bulk: serial iteration
    int sum = 0;
    ex::sync_wait(ex::just(0) | ex::bulk(5, [&sum](int i, int&) { sum += i; }));
    assert(sum == 0+1+2+3+4);

    // start_detached: fire and forget
    int counter = 0;
    ex::start_detached(ex::just() | ex::then([&counter] { counter++; }));
    assert(counter == 1);

    return 0;
}
