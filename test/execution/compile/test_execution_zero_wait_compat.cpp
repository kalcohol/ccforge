#include <forge/execution_extensions.hpp>
int check_forge() {
    if (forge::sync_wait(std::execution::just_stopped())) return 1;
    try { (void)forge::sync_wait(std::execution::just_error(7)); }
    catch (int n) { return n == 7 ? 0 : 2; }
    return 3;
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

#if FORGE_DEPRECATION_CASE == 0 || FORGE_DEPRECATION_CASE == 1
    if (std::this_thread::sync_wait(std::execution::just_stopped())) return 4;
#endif
#if FORGE_DEPRECATION_CASE == 0 || FORGE_DEPRECATION_CASE == 2
    try { (void)std::this_thread::sync_wait(std::execution::just_error(7)); return 5; } catch (int n) { if (n != 7) return 6; }
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
