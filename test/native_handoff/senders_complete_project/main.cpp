#include <execution>
#include <forge/any_stop_token.hpp>

#ifndef FORGE_HAS_NATIVE_SENDERS
#error The complete senders fixture must select native stand-aside.
#endif

int main() {
    std::inplace_stop_source source;
    forge::any_stop_token stop_token{source.get_token()};
    auto result = std::this_thread::sync_wait(std::execution::just(42));
    return result && std::get<0>(*result) == 42 &&
            stop_token.stop_possible() ? 0 : 1;
}
