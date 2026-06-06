#include <forge/accel/cpu/context.hpp>

#include <type_traits>

int main() {
    static_assert(std::is_trivially_copyable_v<int>);
    return sizeof(forge::accel::cpu::device_buffer<int>) == 0 ? 1 : 0;
}
