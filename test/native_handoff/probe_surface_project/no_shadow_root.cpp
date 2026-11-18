#include <forge/linalg.hpp>
#include <forge/unique_resource.hpp>

#if __has_include(<forge_native_execution.hpp>)
#error The Forge backport include root must be absent when no backport is needed.
#endif

#if defined(FORGE_TEST_EXPECT_NO_BASE_MDSPAN)
#if !defined(FORGE_NO_BASE_MDSPAN)
#error The unavailable base mdspan decision must reach the Forge consumer.
#endif
#if !__has_include(<linalg>) || !__has_include(<mdspan>)
#error The unavailable-header fixtures must be on the consumer's include path.
#endif
#endif

int main() {
    auto resource = forge::make_unique_resource_checked(42, -1, [](int) noexcept {});
    return resource.get() == 42 ? 0 : 1;
}
