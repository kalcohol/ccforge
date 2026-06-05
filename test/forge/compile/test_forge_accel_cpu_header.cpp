#include <forge/accel/cpu.hpp>

#include <cstddef>
#include <type_traits>

int main() {
    auto options = forge::accel::cpu::context_options{};
    options.thread_count = 1;
    options.device_count = 1;

    static_assert(std::is_class_v<forge::accel::cpu::context>);
    static_assert(std::is_class_v<forge::accel::cpu::queue>);
    static_assert(std::is_class_v<forge::accel::cpu::device>);
    static_assert(std::is_class_v<forge::accel::cpu::event>);
    static_assert(std::is_class_v<forge::accel::cpu::host_buffer<std::byte>>);
    static_assert(std::is_class_v<forge::accel::cpu::device_buffer<std::byte>>);
    static_assert(!std::is_move_constructible_v<forge::accel::cpu::context>);
    static_assert(!std::is_move_assignable_v<forge::accel::cpu::context>);
}
