#include <experimental/memory>
#include <memory>

#include <forge/unique_resource.hpp>

int main() {
    auto cleanup = [](int) noexcept {};
#if defined(FORGE_TEST_UNIQUE_RESOURCE_EXPERIMENTAL)
    using resource_type = std::experimental::unique_resource<int, decltype(cleanup)>;
#elif defined(FORGE_TEST_UNIQUE_RESOURCE_PROMOTED)
    using resource_type = std::unique_resource<int, decltype(cleanup)>;
#else
    using resource_type = forge::unique_resource<int, decltype(cleanup)>;
#endif
    [[maybe_unused]] resource_type* probe = nullptr;
#if defined(FORGE_TEST_UNIQUE_RESOURCE_EXPERIMENTAL_FACTORY)
    using std::experimental::make_unique_resource_checked;
#elif defined(FORGE_TEST_UNIQUE_RESOURCE_PROMOTED_FACTORY)
    using std::make_unique_resource_checked;
#endif
    auto checked = forge::make_unique_resource_checked(42, -1, cleanup);
    return checked.get() == 42 ? 0 : 1;
}
