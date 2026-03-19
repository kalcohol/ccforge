#include <memory>

int main() {
    std::unique_ptr<int> value;
    std::unique_resource resource(42, [](int) noexcept {});
    auto checked = std::make_unique_resource_checked(42, -1, [](int) noexcept {});

    return value ? resource.get() + checked.get() : 0;
}
