#include <forge/accel.hpp>
#include <forge/execution.hpp>
#include <execution>
#include <cassert>
#include <exception>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

int main() {
    forge::accel::context ctx;
    auto q = ctx.get_queue();
    forge::accel::device_buffer<int> device{ctx, 1};
    std::vector<int> input{1, 2};

    using command = std::execution::completion_signatures<
        std::execution::set_value_t(),
        std::execution::set_error_t(forge::accel::error),
        std::execution::set_stopped_t()>;

    forge::erased_sender<command> work{
        forge::accel::copy_to_device_typed(
            q,
            device,
            std::span<const int>{input})};

    auto result = forge::wait_result(std::move(work));
    assert(result.has_error());

    auto* error = result.error_if<forge::accel::error>();
    assert(error != nullptr);
    assert(error->kind == forge::accel::error_kind::size_mismatch);
    assert(error->cause);
    try {
        std::rethrow_exception(error->cause);
    } catch (const std::runtime_error&) {
        return 0;
    }
    return 1;
}
