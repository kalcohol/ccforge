#include <forge/accel.hpp>
#include <forge/execution.hpp>
#include <execution>
#include "example_support.hpp"
#include <exception>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

int main() {
    forge::accel::mock::context ctx;
    auto q = ctx.get_queue();
    forge::accel::mock::device_buffer<int> device{ctx, 1};
    std::vector<int> input{1, 2};

    using command = std::execution::completion_signatures<
        std::execution::set_value_t(),
        std::execution::set_error_t(forge::accel::error),
        std::execution::set_stopped_t()>;

    forge::erased_sender<command> work{
        forge::accel::mock::copy_to_device_typed(
            q,
            device,
            std::span<const int>{input})};

    auto result = forge::wait_result(std::move(work));
    forge_example::require(result.has_error());

    auto* error = result.error_if<forge::accel::error>();
    forge_example::require(error != nullptr);
    forge_example::require(error->kind == forge::accel::error_kind::size_mismatch);
    forge_example::require(error->cause);
    try {
        std::rethrow_exception(error->cause);
    } catch (const std::runtime_error&) {
        return 0;
    }
    return 1;
}
