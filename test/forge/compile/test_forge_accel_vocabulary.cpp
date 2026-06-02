#include <forge/accel/error.hpp>
#include <forge/accel/vocabulary.hpp>
#include <type_traits>

int main() {
    static_assert(std::is_trivially_copyable_v<forge::accel::device_id>);
    static_assert(std::is_trivially_copyable_v<forge::accel::context_id>);
    static_assert(std::is_trivially_copyable_v<forge::accel::stream_id>);
    static_assert(std::is_trivially_copyable_v<forge::accel::session_id>);
    static_assert(std::is_trivially_copyable_v<forge::accel::request_id>);
    static_assert(std::is_trivially_copyable_v<forge::accel::event_id>);
    static_assert(std::is_trivially_copyable_v<forge::accel::device_epoch>);
    static_assert(std::is_trivially_copyable_v<forge::accel::worker_generation>);
    static_assert(std::is_trivially_copyable_v<forge::accel::worker_key>);
    static_assert(sizeof(forge::accel::request_id) == sizeof(std::uint64_t));
    static_assert(std::is_same_v<decltype(forge::accel::device_info::available), bool>);
    static_assert(forge::accel::memory_kind::host != forge::accel::memory_kind::device);
    static_assert(forge::accel::queue_kind::compute != forge::accel::queue_kind::copy);
    static_assert(forge::accel::copy_kind::host_to_device != forge::accel::copy_kind::device_to_host);

    static_assert(forge::accel::context_id{1} == forge::accel::context_id{1});
    static_assert(forge::accel::stream_id{1} != forge::accel::stream_id{2});

    forge::accel::device_info info{
        .id = forge::accel::device_id{7},
        .ordinal = 2,
        .available = true,
    };
    forge::accel::worker_key key{
        .device = forge::accel::device_id{7},
        .session = forge::accel::session_id{3},
        .context = forge::accel::context_id{5},
        .generation = forge::accel::worker_generation{11},
    };
    forge::accel::model_io_info io{.inputs = 1, .outputs = 2};
    forge::accel::error error{.kind = forge::accel::error_kind::stale_session};

    return static_cast<int>(
               info.id.value + info.ordinal + key.session.value + key.context.value +
               key.generation.value + io.inputs + io.outputs) +
        (error.kind == forge::accel::error_kind::stale_session ? 0 : 1);
}
