#include <forge/unique_resource.hpp>

#include <memory>

int main() {
    std::unique_ptr<int> value;
    forge::unique_resource resource(42, [](int) noexcept {});
    auto checked = forge::make_unique_resource_checked(42, -1, [](int) noexcept {});

    return value ? resource.get() + checked.get() : 0;
}
