#include <utility>

#include <type_traits>

#if !defined(__cpp_lib_constant_wrapper) || __cpp_lib_constant_wrapper < 202606L
#  error std::constant_wrapper must expose the P4206 feature-test value
#endif

using result_t = decltype(std::cw<2> + std::cw<3>);
static_assert(std::is_same_v<result_t, std::constant_wrapper<5>>);

int main() {
    return result_t::value;
}
