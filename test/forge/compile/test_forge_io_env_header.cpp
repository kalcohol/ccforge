#include <forge/io/env.hpp>

#include <memory_resource>
#include <type_traits>

static_assert(std::is_default_constructible_v<forge::io::executor_ref>);
static_assert(std::is_default_constructible_v<forge::io::io_env>);

int main() {
    forge::io::io_env env{};
    return env.memory == std::pmr::get_default_resource() ? 0 : 1;
}
