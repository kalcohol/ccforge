#include <forge/execution_extensions.hpp>
int check_forge() {
    std::execution::simple_counting_scope a;
    std::execution::counting_scope b;
    static_assert(noexcept(forge::scope_count(a)));
    static_assert(noexcept(forge::scope_is_closed(b)));
    if (forge::scope_count(a) != 0 || forge::scope_is_closed(b)) return 1;
    a.close(); b.close();
    return !forge::scope_is_closed(a) || !forge::scope_is_closed(b) || forge::scope_count(b) != 0;
}
#if !defined(FORGE_DEPRECATION_CASE)
#define FORGE_DEPRECATION_CASE 0
#endif
#if FORGE_DEPRECATION_CASE == 0
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4996)
#elif defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
#endif
int main() {
    std::execution::simple_counting_scope a;
    std::execution::counting_scope b;
#if FORGE_DEPRECATION_CASE == 0 || FORGE_DEPRECATION_CASE == 1
    if (a.count() != 0) return 2;
#endif
#if FORGE_DEPRECATION_CASE == 0 || FORGE_DEPRECATION_CASE == 2
    if (a.is_closed()) return 3;
#endif
#if FORGE_DEPRECATION_CASE == 0 || FORGE_DEPRECATION_CASE == 3
    if (b.count() != 0) return 4;
#endif
#if FORGE_DEPRECATION_CASE == 0 || FORGE_DEPRECATION_CASE == 4
    if (b.is_closed()) return 5;
#endif
    return check_forge();
}
#if FORGE_DEPRECATION_CASE == 0
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
#endif
