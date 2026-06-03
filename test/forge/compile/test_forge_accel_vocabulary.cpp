#include <forge/accel/error.hpp>
#include <forge/accel/protocol.hpp>
#include <forge/accel/vocabulary.hpp>
#include <type_traits>

int main() {
    static_assert(std::is_trivially_copyable_v<forge::accel::device_id>);
    static_assert(std::is_trivially_copyable_v<forge::accel::context_id>);
    static_assert(std::is_trivially_copyable_v<forge::accel::stream_id>);
    static_assert(std::is_trivially_copyable_v<forge::accel::session_id>);
    static_assert(std::is_trivially_copyable_v<forge::accel::request_id>);
    static_assert(std::is_trivially_copyable_v<forge::accel::event_id>);
    static_assert(std::is_trivially_copyable_v<forge::accel::event_generation>);
    static_assert(std::is_trivially_copyable_v<forge::accel::endpoint_id>);
    static_assert(std::is_trivially_copyable_v<forge::accel::module_id>);
    static_assert(std::is_trivially_copyable_v<forge::accel::device_epoch>);
    static_assert(std::is_trivially_copyable_v<forge::accel::worker_generation>);
    static_assert(std::is_trivially_copyable_v<forge::accel::worker_key>);
    static_assert(sizeof(forge::accel::request_id) == sizeof(std::uint64_t));
    static_assert(std::is_same_v<decltype(forge::accel::device_info::available), bool>);
    static_assert(std::is_same_v<decltype(forge::accel::device_info::total_memory), std::uint64_t>);
    static_assert(std::is_same_v<decltype(forge::accel::device_info::capability_version), std::uint32_t>);
    static_assert(std::is_same_v<decltype(forge::accel::device_info::max_queues), std::uint32_t>);
    static_assert(forge::accel::memory_kind::host != forge::accel::memory_kind::device);
    static_assert(forge::accel::queue_kind::compute != forge::accel::queue_kind::copy);
    static_assert(forge::accel::copy_kind::host_to_device != forge::accel::copy_kind::device_to_host);
    static_assert(forge::accel::lifecycle_state::online != forge::accel::lifecycle_state::lost);
    static_assert(forge::accel::error_kind_to_string(
        forge::accel::error_kind::host_lost)[0] == 'h');
    static_assert(forge::accel::command_status_to_string(
        forge::accel::command_status::timed_out)[0] == 't');

    static_assert(forge::accel::context_id{1} == forge::accel::context_id{1});
    static_assert(forge::accel::stream_id{1} != forge::accel::stream_id{2});
    static_assert(forge::accel::event_generation{1} < forge::accel::event_generation{2});
    static_assert(forge::accel::endpoint_id{1} == forge::accel::endpoint_id{1});
    static_assert(forge::accel::module_id{1} != forge::accel::module_id{2});

    forge::accel::device_info info{
        .id = forge::accel::device_id{7},
        .ordinal = 2,
        .available = true,
        .total_memory = 1024,
        .capability_version = 1,
        .max_queues = 4,
    };
    forge::accel::worker_key key{
        .device = forge::accel::device_id{7},
        .session = forge::accel::session_id{3},
        .context = forge::accel::context_id{5},
        .generation = forge::accel::worker_generation{11},
    };
    forge::accel::model_io_info io{.inputs = 1, .outputs = 2};
    forge::accel::error error{.kind = forge::accel::error_kind::stale_session};
    {
        forge::accel::current_device_guard guard{forge::accel::device_id{9}};
        if (!forge::accel::current_device().has_value()) {
            return 1;
        }
    }

    return static_cast<int>(
               info.id.value + info.ordinal + key.session.value + key.context.value +
               key.generation.value + io.inputs + io.outputs) +
        (error.kind == forge::accel::error_kind::stale_session ? 0 : 1);
}
