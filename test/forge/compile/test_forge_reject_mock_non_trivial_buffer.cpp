#include <forge/accel/mock/context.hpp>

#include <string>

int main() {
    return static_cast<int>(
        sizeof(forge::accel::mock::device_buffer<std::string>));
}
