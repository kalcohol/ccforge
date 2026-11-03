#include <version>
#include <execution>
#include <forge/any_stop_token.hpp>
#include <stop_token>

#ifndef FORGE_HAS_NATIVE_SENDERS
#error The complete senders fixture must select native stand-aside.
#endif

int main() {
#if defined(__cpp_lib_jthread) && __cpp_lib_jthread >= 201911L
    std::stop_source native_source;
    std::stop_token native_token = native_source.get_token();
    std::stop_callback native_callback(native_token, []() noexcept {});
    if (!native_token.stop_possible()) {
        return 2;
    }
#endif

    std::inplace_stop_source source;
    forge::any_stop_token stop_token{source.get_token()};
    auto result = std::this_thread::sync_wait(std::execution::just(42));
    return result && std::get<0>(*result) == 42 &&
            stop_token.stop_possible() ? 0 : 1;
}
