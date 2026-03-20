#include <execution>

#include <iostream>
#include <tuple>
#include <utility>

int main() {
    auto sender =
        std::execution::just(10) |
        std::execution::then([](int v) { return v + 1; });

    auto result = std::execution::sync_wait(std::move(sender));
    if (!static_cast<bool>(result)) {
        return 1;
    }

    std::cout << std::get<0>(*result) << '\n';
    return 0;
}

