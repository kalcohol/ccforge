#include <execution>
#include <tuple>
#include <type_traits>
#include <utility>

#if __has_include(<forge/execution.hpp>) || __has_include(<forge/unique_resource.hpp>)
#error "forge::std must not expose include/forge extension headers"
#endif

int main() {
    auto result = std::this_thread::sync_wait(std::execution::just(7));
    if (!result || std::get<0>(*result) != 7) {
        return 1;
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
