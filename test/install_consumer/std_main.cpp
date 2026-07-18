#include <execution>
#include <memory>
#include <tuple>
#include <type_traits>
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
#if __cpp_lib_constant_wrapper >= 202606L
    static_assert(std::is_same_v<
                  std::remove_cv_t<decltype(std::cw<5>)>,
                  std::constant_wrapper<5>>);
    static_assert(std::is_same_v<
                  decltype(std::cw<2> + std::cw<3>),
                  std::constant_wrapper<5>>);
#endif

    return 0;
}
