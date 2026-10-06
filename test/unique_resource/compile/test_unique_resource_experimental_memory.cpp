#include <experimental/memory>
#include <memory>
#include <experimental/memory>

#include <type_traits>

#if defined(__GLIBCXX__)
// libstdc++ ships observer_ptr even when it has no native unique_resource.
#ifndef __cpp_lib_experimental_observer_ptr
#error "Forge must retain the platform experimental memory header"
#endif
static_assert(std::is_same_v<decltype(std::experimental::make_observer(static_cast<int*>(nullptr))),
    std::experimental::observer_ptr<int>>);
#endif

int main() {
    int value = 7;
    std::unique_resource resource(&value, [](int*) noexcept {});
    auto checked = std::make_unique_resource_checked(&value, static_cast<int*>(nullptr),
        [](int*) noexcept {});
    if (resource.get() != checked.get()) {
        return 1;
    }
#if defined(__cpp_lib_experimental_observer_ptr)
    const auto observed = std::experimental::make_observer(&value);
    if (observed.get() != resource.get() || *observed != 7) {
        return 1;
    }
#endif
    return 0;
}
