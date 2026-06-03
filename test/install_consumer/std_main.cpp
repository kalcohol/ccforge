#include <execution>
#include <memory>
#include <tuple>
#include <utility>

#if __has_include(<forge/execution.hpp>)
#error "forge::std must not expose include/forge extension headers"
#endif

namespace {

struct cleanup_counter {
    int* calls;

    void operator()(int) const noexcept {
        ++*calls;
    }
};

} // namespace

int main() {
    auto result = std::execution::sync_wait(std::execution::just(7));
    if (!result || std::get<0>(*result) != 7) {
        return 1;
    }

    int cleanup_count = 0;
    {
        std::unique_resource resource(9, cleanup_counter{&cleanup_count});
        if (resource.get() != 9) {
            return 2;
        }
    }
    if (cleanup_count != 1) {
        return 3;
    }

    static_assert(std::constant_wrapper<5>::value == 5);

    return 0;
}
