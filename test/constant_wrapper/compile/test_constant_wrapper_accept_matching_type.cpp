#include <concepts>
#include <type_traits>
#include <utility>

using matching_wrapper = std::constant_wrapper<42, int>;

static_assert(matching_wrapper::value == 42);
static_assert(std::same_as<typename matching_wrapper::value_type, int>);

int main() {}
