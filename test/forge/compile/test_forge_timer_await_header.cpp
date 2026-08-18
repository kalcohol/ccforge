#include <forge/io/timer_await.hpp>

int main() {
#if defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L
    forge::timer_context context;
    auto task = forge::io::async_sleep_for(
        context,
        std::chrono::milliseconds{0});
    (void)task;
#endif
    return 0;
}

