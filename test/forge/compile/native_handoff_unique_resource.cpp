#include <forge/unique_resource.hpp>

#if __has_include(<forge_native_execution.hpp>)
#error The Forge backport include root must be absent when no backport is needed.
#endif

int main() {
    auto resource = forge::make_unique_resource_checked(42, -1, [](int) noexcept {});
    return resource.get() == 42 ? 0 : 1;
}
