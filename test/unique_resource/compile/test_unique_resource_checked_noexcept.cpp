#include <type_traits>
#include <utility>

#include <memory>

namespace {

struct comparison_may_throw {
    comparison_may_throw() noexcept = default;
    comparison_may_throw(const comparison_may_throw&) noexcept = default;
};

bool operator==(const comparison_may_throw&, const comparison_may_throw&) noexcept(false);

void cleanup_checked(const comparison_may_throw&) noexcept {}

} // namespace

static_assert(noexcept(std::make_unique_resource_checked(
    std::declval<comparison_may_throw>(),
    std::declval<const comparison_may_throw&>(),
    &cleanup_checked)),
    "make_unique_resource_checked noexcept should not depend on operator== noexcept");

int main() {
    return 0;
}
