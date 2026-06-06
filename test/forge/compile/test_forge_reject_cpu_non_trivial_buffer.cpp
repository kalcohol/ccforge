#include <forge/accel/cpu/context.hpp>

#include <string>

int main() {
    return static_cast<int>(
        sizeof(forge::accel::cpu::device_buffer<std::string>));
}
