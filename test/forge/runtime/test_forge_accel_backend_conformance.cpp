#include <gtest/gtest.h>
#include <forge/accel.hpp>
#include <forge/erased_sender.hpp>
#include <forge/start_detached.hpp>
#include <forge/wait_result.hpp>
#include "forge_accel_backend_conformance.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <execution>
#include <optional>
#include <span>
#include <tuple>
#include <vector>

namespace {

using mock_backend = forge_test::accel_conformance::mock_backend_adapter;
using namespace std::chrono_literals;

template<class Backend>
class AccelBackendPortableConformanceTest : public ::testing::Test {};

using PortableBackends = ::testing::Types<
    forge_test::accel_conformance::mock_backend_adapter,
    forge_test::accel_conformance::cpu_backend_adapter>;

TYPED_TEST_SUITE(AccelBackendPortableConformanceTest, PortableBackends);

struct request_packet {
    int value = 0;
};

struct response_packet {
    int value = 0;
};

auto payload(std::initializer_list<unsigned char> values)
    -> forge::accel::protocol_payload {
    forge::accel::protocol_payload out;
    out.reserve(values.size());
    for (auto value : values) {
        out.push_back(static_cast<std::byte>(value));
    }
    return out;
}

auto route() -> forge::accel::protocol_route {
    return forge::accel::protocol_route{
        .source = forge::accel::endpoint_id{1},
        .destination = forge::accel::endpoint_id{2},
    };
}

auto meta(forge::accel::request_id request) -> forge::accel::protocol_meta {
    return forge::accel::protocol_meta{
        .request = request,
        .session = forge::accel::session_id{3},
        .context = forge::accel::context_id{4},
        .stream = forge::accel::stream_id{5},
    };
}

template<class Error>
auto expect_operation_error_kind(
    Error&& callable,
    forge::accel::error_kind expected) -> void {
    try {
        callable();
        FAIL() << "expected forge::accel::operation_error";
    } catch (const forge::accel::operation_error& error) {
        EXPECT_EQ(error.kind(), expected);
    }
}

} // namespace

TYPED_TEST(AccelBackendPortableConformanceTest, BasicQueueCopySubmitAndFence) {
    using backend = TypeParam;
    auto ctx = backend::make_context();
    auto q = backend::get_queue(ctx, forge::accel::queue_kind::compute);
    auto device = backend::template make_device_buffer<int>(ctx, 4);
    std::vector<int> input{1, 2, 3, 4};
    std::vector<int> output(4);

    ASSERT_TRUE(backend::sync_ok(
        backend::copy_to_device(q, device, std::span<const int>{input})));
    ASSERT_TRUE(backend::sync_ok(backend::submit(q, [&] {
        for (auto& value : device.span()) {
            value *= 2;
        }
    })));
    ASSERT_TRUE(backend::sync_ok(backend::fence(q)));
    ASSERT_TRUE(backend::sync_ok(
        backend::copy_to_host(q, std::span<int>{output}, device)));

    EXPECT_EQ(output, (std::vector<int>{2, 4, 6, 8}));
}

TYPED_TEST(AccelBackendPortableConformanceTest, CrossQueueEventOrdersCopyComputeCopy) {
    using backend = TypeParam;
    auto ctx = backend::make_context(typename backend::context_options{
        // The pipeline can have two unready cross-queue waits at once; keep one
        // spare worker available for record_event signal work.
        .thread_count = 3,
        .queue_capacity = std::nullopt,
    });
    auto copy = backend::get_queue(ctx, forge::accel::queue_kind::copy);
    auto compute = backend::get_queue(ctx, forge::accel::queue_kind::compute);
    auto uploaded = backend::make_event();
    auto computed = backend::make_event();
    auto device = backend::template make_device_buffer<int>(ctx, 3);
    std::vector<int> input{3, 5, 7};
    std::vector<int> output(3);

    auto [upload_op, upload_state] = backend::connect_async(
        backend::copy_to_device(copy, device, std::span<const int>{input}));
    auto [record_upload_op, record_upload_state] = backend::connect_async(
        backend::record_event(copy, uploaded));
    auto [wait_upload_op, wait_upload_state] = backend::connect_async(
        backend::wait_event(compute, uploaded));
    auto [compute_op, compute_state] = backend::connect_async(
        backend::submit(compute, [&] {
            for (auto& value : device.span()) {
                value += 1;
            }
        }));
    auto [record_compute_op, record_compute_state] = backend::connect_async(
        backend::record_event(compute, computed));
    auto [wait_compute_op, wait_compute_state] = backend::connect_async(
        backend::wait_event(copy, computed));
    auto [download_op, download_state] = backend::connect_async(
        backend::copy_to_host(copy, std::span<int>{output}, device));

    std::execution::start(upload_op);
    std::execution::start(record_upload_op);
    std::execution::start(wait_upload_op);
    std::execution::start(compute_op);
    std::execution::start(record_compute_op);
    std::execution::start(wait_compute_op);
    std::execution::start(download_op);
    ctx.wait();

    ASSERT_TRUE(backend::wait_done(upload_state));
    ASSERT_TRUE(backend::wait_done(record_upload_state));
    ASSERT_TRUE(backend::wait_done(wait_upload_state));
    ASSERT_TRUE(backend::wait_done(compute_state));
    ASSERT_TRUE(backend::wait_done(record_compute_state));
    ASSERT_TRUE(backend::wait_done(wait_compute_state));
    ASSERT_TRUE(backend::wait_done(download_state));
    EXPECT_TRUE(upload_state->value);
    EXPECT_TRUE(record_upload_state->value);
    EXPECT_TRUE(wait_upload_state->value);
    EXPECT_TRUE(compute_state->value);
    EXPECT_TRUE(record_compute_state->value);
    EXPECT_TRUE(wait_compute_state->value);
    EXPECT_TRUE(download_state->value);
    EXPECT_EQ(output, (std::vector<int>{4, 6, 8}));
}

TYPED_TEST(AccelBackendPortableConformanceTest, SameQueueWaitBeforeRecordStopsCleanly) {
    using backend = TypeParam;
    auto ctx = backend::make_context(typename backend::context_options{
        .thread_count = 2,
        .queue_capacity = std::nullopt,
    });
    auto q = backend::get_queue(ctx);
    auto ev = backend::make_event();

    auto [wait_op, wait_state] = backend::connect_async(
        backend::wait_event(q, ev));
    std::execution::start(wait_op);
    EXPECT_FALSE(backend::wait_done(wait_state, 50ms));

    auto [record_op, record_state] = backend::connect_async(
        backend::record_event(q, ev));
    std::execution::start(record_op);
    EXPECT_FALSE(backend::wait_done(record_state, 50ms));

    ctx.request_stop();

    ASSERT_TRUE(backend::wait_done(wait_state));
    ASSERT_TRUE(backend::wait_done(record_state));
    EXPECT_TRUE(wait_state->stopped);
    EXPECT_TRUE(record_state->stopped);
    EXPECT_FALSE(ev.ready());
}

TYPED_TEST(AccelBackendPortableConformanceTest, CapacityFullCompletesStoppedWithoutLeakingWork) {
    using backend = TypeParam;
    auto ctx = backend::make_context(typename backend::context_options{
        .thread_count = 1,
        .queue_capacity = 1,
    });
    auto q = backend::get_queue(ctx);
    typename backend::blocking_gate gate;

    auto [first_op, first_state] = backend::connect_async(
        backend::submit(q, [&] {
            gate.mark_started_and_wait();
        }));
    std::execution::start(first_op);
    ASSERT_TRUE(gate.wait_started());

    auto rejected = std::execution::sync_wait(backend::submit(q, [] {}));
    EXPECT_FALSE(rejected.has_value());

    gate.release_gate();
    ASSERT_TRUE(backend::wait_done(first_state));
    EXPECT_TRUE(first_state->value);
    ctx.wait();
}

TYPED_TEST(AccelBackendPortableConformanceTest, SizeMismatchIsClassified) {
    using backend = TypeParam;
    auto ctx = backend::make_context();
    auto q = backend::get_queue(ctx);
    auto small = backend::template make_device_buffer<int>(ctx, 2);
    std::vector<int> too_large{1, 2, 3};

    EXPECT_THROW(
        (void)std::execution::sync_wait(
            backend::copy_to_device(q, small, std::span<const int>{too_large})),
        std::runtime_error);
}

TYPED_TEST(
    AccelBackendPortableConformanceTest,
    TypedSizeMismatchCrossesErasureAndWaitResult) {
    using backend = TypeParam;
    using completions = std::execution::completion_signatures<
        std::execution::set_value_t(),
        std::execution::set_error_t(forge::accel::error),
        std::execution::set_stopped_t()>;

    auto ctx = backend::make_context();
    auto q = backend::get_queue(ctx);
    auto small = backend::template make_device_buffer<int>(ctx, 2);
    std::vector<int> too_large{1, 2, 3};

    forge::erased_sender<completions> erased{
        backend::copy_to_device_typed(
            q,
            small,
            std::span<const int>{too_large})};
    auto result = forge::wait_result(std::move(erased));

    ASSERT_TRUE(result.has_error());
    auto* error = result.error_if<forge::accel::error>();
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->kind, forge::accel::error_kind::size_mismatch);
}

TYPED_TEST(
    AccelBackendPortableConformanceTest,
    BackendWorkCanWaitForOwnContextWithoutDeadlock) {
    using backend = TypeParam;
    auto ctx = backend::make_context(typename backend::context_options{
        .thread_count = 1,
        .queue_capacity = std::nullopt,
    });
    auto q = backend::get_queue(ctx);
    bool reached = false;

    ASSERT_TRUE(backend::sync_ok(backend::submit(q, [&] {
        ctx.wait();
        reached = true;
    })));

    EXPECT_TRUE(reached);
}

TEST(AccelBackendMockExtensionTest, CachedMemoryRequiresFlush) {
    using backend = mock_backend;
    auto ctx = backend::make_context();
    auto q = backend::get_queue(ctx);
    auto cached = backend::make_device_buffer<int>(
        ctx,
        2,
        forge::accel::memory_kind::cached_device);
    std::vector<int> input{5, 8};
    std::vector<int> output(2);
    ASSERT_TRUE(backend::sync_ok(
        backend::copy_to_device(q, cached, std::span<const int>{input})));

    expect_operation_error_kind(
        [&] {
            (void)std::execution::sync_wait(
                backend::copy_to_host(q, std::span<int>{output}, cached));
        },
        forge::accel::error_kind::coherence_required);

    ASSERT_TRUE(backend::sync_ok(backend::flush(q, cached)));
    ASSERT_TRUE(backend::sync_ok(
        backend::copy_to_host(q, std::span<int>{output}, cached)));
    EXPECT_EQ(output, input);
}

TEST(AccelBackendMockExtensionTest, DeviceLostResetAndStaleSessionAreClassified) {
    using backend = mock_backend;
    auto ctx = backend::make_context();
    auto device = backend::get_device(ctx);
    auto old_session = backend::open_session(device);

    device.mark_lost();
    expect_operation_error_kind(
        [&] {
            (void)std::execution::sync_wait(
                backend::submit(old_session, [] {}));
        },
        forge::accel::error_kind::device_lost);

    device.reset();
    expect_operation_error_kind(
        [&] {
            (void)std::execution::sync_wait(
                backend::submit(old_session, [] {}));
        },
        forge::accel::error_kind::stale_session);

    auto recovered = backend::open_session(device);
    ASSERT_TRUE(backend::sync_ok(backend::submit(recovered, [] {})));
}

TEST(AccelBackendMockExtensionTest, DrainFreezeAndWorkerFaultAreClassified) {
    using backend = mock_backend;
    auto ctx = backend::make_context();
    auto device = backend::get_device(ctx);
    auto session = backend::open_session(device);
    const auto generation = device.current_worker_generation();

    device.begin_drain_freeze();
    expect_operation_error_kind(
        [&] {
            (void)std::execution::sync_wait(
                backend::submit(session, [] {}));
        },
        forge::accel::error_kind::drain_freeze);

    device.complete_drain();
    ASSERT_TRUE(backend::sync_ok(backend::submit(session, [] {})));

    device.mark_worker_fault();
    expect_operation_error_kind(
        [&] {
            (void)std::execution::sync_wait(
                backend::submit(session, [] {}));
        },
        forge::accel::error_kind::worker_fault);

    EXPECT_FALSE(device.clear_worker_fault(
        forge::accel::worker_generation{generation.value + 9}));
    EXPECT_TRUE(device.clear_worker_fault(device.current_worker_generation()));
    ASSERT_TRUE(backend::sync_ok(backend::submit(session, [] {})));
}

TEST(AccelBackendMockExtensionTest, RequestTimeoutAndLateResponseAreObservable) {
    using backend = mock_backend;
    auto ctx = backend::make_context(backend::context_options{
        .thread_count = 1,
        .queue_capacity = 2,
    });
    auto device = backend::get_device(ctx);
    auto session = backend::open_session(device);
    forge::accel::mock::request_session requests{session};
    backend::blocking_gate gate;

    forge::start_detached(backend::submit(session, [&] {
        gate.mark_started_and_wait();
    }));
    ASSERT_TRUE(gate.wait_started());

    auto result = forge::wait_result(
        requests.submit_request_typed(
            request_packet{7},
            response_packet{},
            [](request_packet& request, response_packet& response) noexcept {
                response.value = request.value + 1;
            },
            forge::accel::mock::request_options{.timeout = 20ms}));

    ASSERT_TRUE(result.has_error());
    auto* error = result.error_if<forge::accel::error>();
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->kind, forge::accel::error_kind::timeout);

    gate.release_gate();
    ctx.wait();
    EXPECT_EQ(requests.pending_count(), 0U);
    EXPECT_EQ(requests.late_response_count(), 1U);
}

TEST(AccelBackendMockExtensionTest, ProtocolSignalsBypassPendingRequestMap) {
    forge::accel::mock::protocol::loopback_transport transport;
    auto request = forge::accel::make_request_envelope(
        route(),
        meta(forge::accel::request_id{9}),
        forge::accel::module_id{1},
        forge::accel::command_id{2},
        payload({1, 2}));
    ASSERT_TRUE(transport.submit_request(request));
    EXPECT_EQ(transport.pending_count(), 1U);

    auto signal = forge::accel::make_signal_envelope(
        route(),
        meta(forge::accel::request_id{0}),
        forge::accel::lifecycle_signal{
            .reason = forge::accel::lifecycle_signal_reason::reset,
            .epoch = forge::accel::device_epoch{3},
            .generation = forge::accel::worker_generation{4},
        });
    ASSERT_TRUE(transport.deliver_signal(std::move(signal)));
    EXPECT_EQ(transport.pending_count(), 1U);

    auto completion = transport.try_recv_completion();
    ASSERT_TRUE(completion.has_value());
    EXPECT_EQ(completion->kind, forge::accel::message_kind::signal);
    ASSERT_TRUE(completion->signal.has_value());
    EXPECT_EQ(
        completion->signal->reason,
        forge::accel::lifecycle_signal_reason::reset);
}

TEST(AccelBackendMockExtensionTest, TraceIsOptionalAndObservesCommandPhases) {
    using backend = mock_backend;
    {
        auto ctx = backend::make_context();
        auto q = backend::get_queue(ctx);
        int value = 0;
        ASSERT_TRUE(backend::sync_ok(backend::submit(q, [&] {
            value = 42;
        })));
        EXPECT_EQ(value, 42);
    }

    backend::trace_sink trace;
    auto ctx = backend::make_context(backend::context_options{
        .thread_count = 1,
        .queue_capacity = std::nullopt,
        .device_count = 1,
        .memory = forge::default_memory_resource(),
        .trace = &trace,
    });
    auto q = backend::get_queue(ctx);
    ASSERT_TRUE(backend::sync_ok(backend::submit(q, [] {})));

    auto events = trace.snapshot();
    ASSERT_EQ(events.size(), 3U);
    EXPECT_EQ(events[0].kind, forge::accel::mock::trace_event_kind::submitted);
    EXPECT_EQ(events[1].kind, forge::accel::mock::trace_event_kind::started);
    EXPECT_EQ(events[2].kind, forge::accel::mock::trace_event_kind::completed);
    EXPECT_LT(events[0].sequence, events[1].sequence);
    EXPECT_LT(events[1].sequence, events[2].sequence);
}

TEST(AccelBackendMockExtensionTest, PacketTypedErrorsCrossErasureAndWaitResult) {
    using backend = mock_backend;
    using packet_t = forge::accel::mock::command_packet<
        request_packet,
        response_packet>;
    using completions = std::execution::completion_signatures<
        std::execution::set_value_t(packet_t),
        std::execution::set_error_t(forge::accel::error),
        std::execution::set_stopped_t()>;

    auto ctx = backend::make_context();
    auto session = backend::open_session(backend::get_device(ctx));

    forge::erased_sender<completions> erased{
        backend::submit_packet_typed(
            session,
            packet_t{
                forge::accel::command_id{77},
                request_packet{1},
                response_packet{}},
            [](request_packet&, response_packet&) noexcept {
                return forge::accel::command_status::failed;
            })};

    auto result = forge::wait_result(std::move(erased));
    ASSERT_TRUE(result.has_error());
    auto* error = result.error_if<forge::accel::error>();
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->kind, forge::accel::error_kind::command_failed);
    EXPECT_EQ(error->status, forge::accel::command_status::failed);
}
