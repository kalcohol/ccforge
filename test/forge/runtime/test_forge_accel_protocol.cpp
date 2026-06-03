#include <gtest/gtest.h>
#include <forge/accel.hpp>

#include <cstddef>
#include <vector>

namespace {

auto bytes(std::initializer_list<unsigned char> values) -> forge::accel::protocol_payload {
    forge::accel::protocol_payload payload;
    payload.reserve(values.size());
    for (auto value : values) {
        payload.push_back(static_cast<std::byte>(value));
    }
    return payload;
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
        .session = forge::accel::session_id{7},
        .context = forge::accel::context_id{11},
        .stream = forge::accel::stream_id{13},
    };
}

} // namespace

TEST(AccelProtocolTest, RequestEnvelopeCreatesCorrelatedResponse) {
    forge::accel::mock::protocol::loopback_transport transport;
    auto request = forge::accel::make_request_envelope(
        route(),
        meta(forge::accel::request_id{42}),
        forge::accel::module_id{3},
        forge::accel::command_id{9},
        bytes({1, 2, 3}));

    ASSERT_TRUE(transport.submit_request(request));
    EXPECT_EQ(transport.pending_count(), 1U);

    auto queued = transport.try_recv_request();
    ASSERT_TRUE(queued.has_value());
    EXPECT_EQ(queued->kind, forge::accel::message_kind::request);
    EXPECT_EQ(queued->meta.request.value, 42U);

    auto response = forge::accel::make_response_envelope(*queued, bytes({4, 5}));
    ASSERT_TRUE(transport.deliver_response(std::move(response)));

    auto completion = transport.try_recv_completion();
    ASSERT_TRUE(completion.has_value());
    EXPECT_EQ(completion->kind, forge::accel::message_kind::response);
    EXPECT_EQ(completion->route.source.value, queued->route.destination.value);
    EXPECT_EQ(completion->route.destination.value, queued->route.source.value);
    EXPECT_EQ(completion->meta.request.value, 42U);
    EXPECT_EQ(completion->module.value, 3U);
    EXPECT_EQ(completion->command.value, 9U);
    EXPECT_EQ(completion->payload, bytes({4, 5}));
    EXPECT_EQ(transport.pending_count(), 0U);
    EXPECT_EQ(transport.late_response_count(), 0U);
}

TEST(AccelProtocolTest, PostedAndNonPostedRequestsShareTransport) {
    forge::accel::mock::protocol::loopback_transport transport;
    auto posted = forge::accel::make_request_envelope(
        route(),
        meta(forge::accel::request_id{50}),
        forge::accel::module_id{1},
        forge::accel::command_id{1});
    auto non_posted = forge::accel::make_request_envelope(
        route(),
        meta(forge::accel::request_id{51}),
        forge::accel::module_id{1},
        forge::accel::command_id{2});

    auto posted_result = transport.submit_posted(std::move(posted));
    ASSERT_TRUE(posted_result);
    EXPECT_EQ(posted_result.mode, forge::accel::call_mode::posted);
    EXPECT_EQ(posted_result.request.value, 50U);

    auto non_posted_result = transport.submit_non_posted(std::move(non_posted));
    ASSERT_TRUE(non_posted_result);
    EXPECT_EQ(non_posted_result.mode, forge::accel::call_mode::non_posted);
    EXPECT_EQ(non_posted_result.request.value, 51U);

    auto first = transport.try_recv_request();
    ASSERT_TRUE(first.has_value());
    auto second = transport.try_recv_request();
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(first->meta.request.value, 50U);
    EXPECT_EQ(second->meta.request.value, 51U);

    ASSERT_TRUE(transport.deliver_response(
        forge::accel::make_response_envelope(*second, bytes({2}))));
    auto completion = transport.try_recv_completion();
    ASSERT_TRUE(completion.has_value());
    EXPECT_EQ(completion->meta.request.value, 51U);
}

TEST(AccelProtocolTest, DuplicateRequestReportsTypedTransportStatus) {
    forge::accel::mock::protocol::loopback_transport transport;
    auto first = forge::accel::make_request_envelope(
        route(),
        meta(forge::accel::request_id{52}),
        forge::accel::module_id{1},
        forge::accel::command_id{1});
    auto second = forge::accel::make_request_envelope(
        route(),
        meta(forge::accel::request_id{52}),
        forge::accel::module_id{1},
        forge::accel::command_id{2});

    ASSERT_TRUE(transport.submit_posted(std::move(first)));
    auto result = transport.submit_non_posted(std::move(second));
    EXPECT_FALSE(result);
    EXPECT_EQ(result.status, forge::accel::transport_status::duplicate_request);
    EXPECT_EQ(result.mode, forge::accel::call_mode::non_posted);
    EXPECT_EQ(result.request.value, 52U);
    EXPECT_EQ(transport.pending_count(), 1U);
}

TEST(AccelProtocolTest, InvalidRequestReportsTypedTransportStatus) {
    forge::accel::mock::protocol::loopback_transport transport;
    auto invalid = forge::accel::protocol_envelope{
        .kind = forge::accel::message_kind::notify,
        .route = route(),
        .meta = meta(forge::accel::request_id{0}),
    };

    auto result = transport.submit_posted(std::move(invalid));
    EXPECT_FALSE(result);
    EXPECT_EQ(result.status, forge::accel::transport_status::invalid_message);
    EXPECT_EQ(result.mode, forge::accel::call_mode::posted);
    EXPECT_EQ(transport.pending_count(), 0U);
}

TEST(AccelProtocolTest, FullRequestQueueReportsNotAccepted) {
    forge::accel::mock::protocol::loopback_transport transport{
        forge::accel::mock::protocol::loopback_transport_options{.capacity = 1}};
    auto first = forge::accel::make_request_envelope(
        route(),
        meta(forge::accel::request_id{53}),
        forge::accel::module_id{1},
        forge::accel::command_id{1});
    auto second = forge::accel::make_request_envelope(
        route(),
        meta(forge::accel::request_id{54}),
        forge::accel::module_id{1},
        forge::accel::command_id{2});

    ASSERT_TRUE(transport.submit_posted(std::move(first)));
    auto result = transport.submit_posted(std::move(second));
    EXPECT_FALSE(result);
    EXPECT_EQ(result.status, forge::accel::transport_status::not_accepted);
    EXPECT_EQ(result.request.value, 54U);
    EXPECT_EQ(transport.pending_count(), 1U);
}

TEST(AccelProtocolTest, FullCompletionQueueKeepsResponsePendingForRetry) {
    forge::accel::mock::protocol::loopback_transport transport{
        forge::accel::mock::protocol::loopback_transport_options{.capacity = 1}};
    auto request = forge::accel::make_request_envelope(
        route(),
        meta(forge::accel::request_id{43}),
        forge::accel::module_id{3},
        forge::accel::command_id{9});

    ASSERT_TRUE(transport.submit_request(request));
    auto queued = transport.try_recv_request();
    ASSERT_TRUE(queued.has_value());

    ASSERT_TRUE(transport.deliver_signal(forge::accel::make_signal_envelope(
        route(),
        forge::accel::protocol_meta{},
        forge::accel::lifecycle_signal{
            .reason = forge::accel::lifecycle_signal_reason::closing,
        })));

    EXPECT_FALSE(transport.deliver_response(
        forge::accel::make_response_envelope(*queued, bytes({1}))));
    EXPECT_EQ(transport.pending_count(), 1U);
    EXPECT_EQ(transport.late_response_count(), 0U);

    ASSERT_TRUE(transport.try_recv_completion().has_value());
    EXPECT_TRUE(transport.deliver_response(
        forge::accel::make_response_envelope(*queued, bytes({2}))));

    auto completion = transport.try_recv_completion();
    ASSERT_TRUE(completion.has_value());
    EXPECT_EQ(completion->kind, forge::accel::message_kind::response);
    EXPECT_EQ(completion->payload, bytes({2}));
    EXPECT_EQ(transport.pending_count(), 0U);
}

TEST(AccelProtocolTest, UnknownResponseIdIsClassifiedLateAndDiscarded) {
    forge::accel::mock::protocol::loopback_transport transport;
    auto unknown = forge::accel::protocol_envelope{
        .kind = forge::accel::message_kind::response,
        .route = route(),
        .meta = meta(forge::accel::request_id{999}),
        .module = forge::accel::module_id{1},
        .command = forge::accel::command_id{2},
        .payload = bytes({9}),
    };

    auto result = transport.deliver_response_result(std::move(unknown));
    EXPECT_FALSE(result);
    EXPECT_EQ(result.status, forge::accel::transport_status::late_response);
    EXPECT_EQ(result.request.value, 999U);
    EXPECT_EQ(transport.late_response_count(), 1U);
    EXPECT_FALSE(transport.try_recv_completion().has_value());
}

TEST(AccelProtocolTest, LifecycleSignalBypassesPendingMap) {
    forge::accel::mock::protocol::loopback_transport transport;
    auto signal = forge::accel::lifecycle_signal{
        .reason = forge::accel::lifecycle_signal_reason::reset,
        .epoch = forge::accel::device_epoch{4},
        .generation = forge::accel::worker_generation{5},
        .diagnostic = "session reset",
    };
    auto envelope = forge::accel::make_signal_envelope(
        route(),
        meta(forge::accel::request_id{0}),
        std::move(signal));

    ASSERT_TRUE(transport.deliver_signal(std::move(envelope)));
    EXPECT_EQ(transport.pending_count(), 0U);

    auto completion = transport.try_recv_completion();
    ASSERT_TRUE(completion.has_value());
    ASSERT_TRUE(completion->signal.has_value());
    EXPECT_EQ(completion->kind, forge::accel::message_kind::signal);
    EXPECT_EQ(
        completion->signal->reason,
        forge::accel::lifecycle_signal_reason::reset);
    EXPECT_EQ(completion->signal->epoch.value, 4U);
    EXPECT_EQ(completion->signal->generation.value, 5U);
    EXPECT_EQ(completion->signal->diagnostic, "session reset");
}

TEST(AccelProtocolTest, RouteAndMetaAreIndependentOfScheduling) {
    forge::accel::mock::protocol::loopback_transport transport;
    auto request = forge::accel::make_request_envelope(
        forge::accel::protocol_route{
            .source = forge::accel::endpoint_id{100},
            .destination = forge::accel::endpoint_id{200},
        },
        forge::accel::protocol_meta{
            .request = forge::accel::request_id{77},
            .session = forge::accel::session_id{88},
            .context = forge::accel::context_id{99},
            .stream = forge::accel::stream_id{123},
        },
        forge::accel::module_id{5},
        forge::accel::command_id{6});

    ASSERT_TRUE(transport.submit_request(request));
    auto queued = transport.try_recv_request();
    ASSERT_TRUE(queued.has_value());
    EXPECT_EQ(queued->route.source.value, 100U);
    EXPECT_EQ(queued->route.destination.value, 200U);
    EXPECT_EQ(queued->meta.session.value, 88U);
    EXPECT_EQ(queued->meta.context.value, 99U);
    EXPECT_EQ(queued->meta.stream.value, 123U);

    ASSERT_TRUE(transport.deliver_response(
        forge::accel::make_response_envelope(*queued)));
    auto completion = transport.try_recv_completion();
    ASSERT_TRUE(completion.has_value());
    EXPECT_EQ(completion->meta.stream.value, 123U);
}
