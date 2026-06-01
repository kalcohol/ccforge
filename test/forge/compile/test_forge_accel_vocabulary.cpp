#include <forge/accel/error.hpp>
#include <forge/accel/vocabulary.hpp>
#include <type_traits>

int main() {
    static_assert(std::is_trivially_copyable_v<forge::accel::device_id>);
    static_assert(std::is_same_v<decltype(forge::accel::device_info::available), bool>);
    static_assert(forge::accel::memory_kind::host != forge::accel::memory_kind::device);
    static_assert(forge::accel::queue_kind::compute != forge::accel::queue_kind::copy);
    static_assert(forge::accel::copy_kind::host_to_device != forge::accel::copy_kind::device_to_host);

    forge::accel::device_info info{
        .id = forge::accel::device_id{7},
        .ordinal = 2,
        .available = true,
    };
    forge::accel::model_io_info io{.inputs = 1, .outputs = 2};
    forge::accel::error error{.kind = forge::accel::error_kind::invalid_context};

    return static_cast<int>(info.id.value + info.ordinal + io.inputs + io.outputs) +
        (error.kind == forge::accel::error_kind::invalid_context ? 0 : 1);
}
