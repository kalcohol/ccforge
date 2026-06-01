#include <forge/accel.hpp>
#include <execution>
#include <cstddef>
#include <iostream>
#include <span>
#include <tuple>
#include <vector>

int main() {
    forge::accel::mock::context ctx;
    forge::accel::mock::model model{forge::accel::mock::model_descriptor{
        .inputs = {
            forge::accel::model_io_descriptor{
                .byte_size = 4,
                .rank = 1,
                .extents = {4, 0, 0, 0},
            },
        },
        .outputs = {
            forge::accel::model_io_descriptor{
                .byte_size = 4,
                .rank = 1,
                .extents = {4, 0, 0, 0},
            },
        },
    }};

    auto session = model.open_session(ctx.get_device());
    forge::accel::mock::model_bindings bindings{model};
    std::vector<std::byte> input{
        std::byte{1},
        std::byte{2},
        std::byte{3},
        std::byte{4},
    };
    std::vector<std::byte> output(4);
    bindings.bind_input(0, std::span<const std::byte>{input});
    bindings.bind_output(0, std::span<std::byte>{output});

    auto result = std::execution::sync_wait(
        forge::accel::mock::execute(session, std::move(bindings)));
    if (!result) {
        return 1;
    }

    std::cout << "mock model output[0]="
              << std::to_integer<int>(output[0]) << "\n";
    return output[0] == std::byte{10} ? 0 : 1;
}
