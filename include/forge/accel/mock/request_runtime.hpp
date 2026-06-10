// MIT License
//
// Copyright (c) 2026 CC Forge Project
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include "context.hpp"
#include "../../timer_context.hpp"
#include "../../start_detached.hpp"

#include <execution>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <exception>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <optional>
#include <stop_token>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace forge::accel::mock {

struct request_options {
    std::optional<std::chrono::steady_clock::duration> timeout = std::nullopt;
};

template<class Request, class Response>
struct request_packet {
    using request_type = Request;
    using response_type = Response;

    request_packet(request_id id, Request request, Response response)
        : id(id)
        , request(std::move(request))
        , response(std::move(response))
    {}

    request_id id{};
    Request request;
    Response response;
    command_status status = command_status::ok;
};

template<class Request, class Response>
request_packet(request_id, Request, Response) -> request_packet<Request, Response>;

namespace __request_detail {

struct __state {
    explicit __state(std::pmr::memory_resource* memory_resource)
        : memory(normalize_memory_resource(memory_resource))
        , pending(std::pmr::polymorphic_allocator<std::uint64_t>{memory})
        , timers(timer_context_options{.memory = memory})
    {}

    ~__state() noexcept {
        timers.shutdown();
        timers.wait();
    }

    [[nodiscard]] auto next_request() noexcept -> request_id {
        return request_id{next.fetch_add(1, std::memory_order_relaxed)};
    }

    void insert(request_id id) {
        std::lock_guard lk{mtx};
        pending.insert(id.value);
    }

    void erase(request_id id) noexcept {
        std::lock_guard lk{mtx};
        pending.erase(id.value);
    }

    void note_late_response() noexcept {
        late_responses.fetch_add(1, std::memory_order_relaxed);
    }

    [[nodiscard]] auto pending_count() const noexcept -> std::size_t {
        std::lock_guard lk{mtx};
        return pending.size();
    }

    [[nodiscard]] auto late_response_count() const noexcept -> std::size_t {
        return late_responses.load(std::memory_order_relaxed);
    }

    void wait() noexcept {
        timers.wait();
    }

    std::pmr::memory_resource* memory;
    mutable std::mutex mtx;
    std::pmr::unordered_set<std::uint64_t> pending;
    std::atomic<std::uint64_t> next{1};
    std::atomic<std::size_t> late_responses{0};
    timer_context timers;
};

template<class R, class Packet>
struct __record {
    __record(std::shared_ptr<__state> state, request_id id, R rcvr)
        : state(std::move(state))
        , id(id)
        , rcvr(std::move(rcvr))
    {}

    void cancel_timeout() noexcept {
        if (timeout_stop) {
            timeout_stop->request_stop();
        }
    }

    void complete_response(Packet packet) noexcept {
        if (done.exchange(true, std::memory_order_acq_rel)) {
            state->note_late_response();
            return;
        }
        cancel_timeout();
        state->erase(id);
        std::execution::set_value(std::move(rcvr), std::move(packet));
    }

    void complete_timeout() noexcept {
        if (done.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        state->erase(id);
        std::execution::set_error(
            std::move(rcvr),
            std::make_exception_ptr(operation_error{
                error_kind::timeout,
                command_status::timed_out,
                "forge::accel::mock request timed out"}));
    }

    void complete_error(std::exception_ptr ep) noexcept {
        if (done.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        cancel_timeout();
        state->erase(id);
        std::execution::set_error(std::move(rcvr), std::move(ep));
    }

    void complete_stopped() noexcept {
        if (done.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        cancel_timeout();
        state->erase(id);
        std::execution::set_stopped(std::move(rcvr));
    }

    std::shared_ptr<__state> state;
    request_id id;
    R rcvr;
    std::shared_ptr<std::inplace_stop_source> timeout_stop;
    std::atomic<bool> done{false};
};

template<class Request, class Response, class Handler>
struct __sender {
    using sender_concept = std::execution::sender_t;
    using packet_t = request_packet<Request, Response>;
    using completion_signatures = std::execution::completion_signatures<
        std::execution::set_value_t(packet_t),
        std::execution::set_error_t(std::exception_ptr),
        std::execution::set_stopped_t()>;

    std::shared_ptr<__state> state;
    device_session session;
    Request request;
    Response response;
    Handler handler;
    request_options options;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept -> completion_signatures {
        return {};
    }

    auto get_env() const noexcept -> std::execution::empty_env {
        return {};
    }

    template<class R>
        requires std::execution::receiver_of<R, completion_signatures>
    struct __op {
        using operation_state_concept = std::execution::operation_state_t;
        using packet_t = request_packet<Request, Response>;
        using command_packet_t = command_packet<Request, Response>;

        __op(
            std::shared_ptr<__state> state,
            device_session session,
            Request request,
            Response response,
            Handler handler,
            request_options options,
            R rcvr)
            : state_(std::move(state))
            , session_(std::move(session))
            , request_(std::move(request))
            , response_(std::move(response))
            , handler_(std::move(handler))
            , options_(options)
            , rcvr_(std::move(rcvr))
        {}

        __op(__op&&) = delete;
        __op& operator=(__op&&) = delete;
        __op(const __op&) = delete;
        __op& operator=(const __op&) = delete;

        void start() & noexcept {
            try {
                auto id = state_->next_request();
                using allocator_t = std::pmr::polymorphic_allocator<
                    __record<R, packet_t>>;
                record_ = std::allocate_shared<__record<R, packet_t>>(
                    allocator_t{state_->memory},
                    state_,
                    id,
                    std::move(*rcvr_));
                state_->insert(id);

                if (options_.timeout) {
                    record_->timeout_stop =
                        std::allocate_shared<std::inplace_stop_source>(
                            std::pmr::polymorphic_allocator<
                                std::inplace_stop_source>{state_->memory});
                    auto timeout_env = std::execution::make_prop(
                        std::execution::get_stop_token_t{},
                        record_->timeout_stop->get_token());
                    auto timeout =
                        (state_->timers.schedule_after(*options_.timeout)
                         | std::execution::write_env(std::move(timeout_env)))
                        | std::execution::then([record = record_]() noexcept {
                              record->complete_timeout();
                          })
                        | std::execution::upon_stopped([]() noexcept {});
                    forge::start_detached(std::move(timeout));
                }

                auto response = submit_packet(
                    session_,
                    command_packet_t{
                        command_id{id.value},
                        std::move(*request_),
                        std::move(*response_)},
                    std::move(*handler_),
                    command_options{})
                    | std::execution::then([record = record_](
                          command_packet_t packet) noexcept {
                          record->complete_response(packet_t{
                              record->id,
                              std::move(packet.request),
                              std::move(packet.response)});
                      })
                    | std::execution::upon_error([record = record_](
                          std::exception_ptr ep) noexcept {
                          record->complete_error(std::move(ep));
                      })
                    | std::execution::upon_stopped([record = record_]() noexcept {
                          record->complete_stopped();
                      });
                forge::start_detached(std::move(response));
            } catch (...) {
                if (record_) {
                    record_->complete_error(std::current_exception());
                } else if (rcvr_) {
                    std::execution::set_error(
                        std::move(*rcvr_),
                        std::current_exception());
                    rcvr_.reset();
                }
            }
        }

        std::shared_ptr<__state> state_;
        device_session session_;
        std::optional<Request> request_;
        std::optional<Response> response_;
        std::optional<Handler> handler_;
        request_options options_;
        std::optional<R> rcvr_;
        std::shared_ptr<__record<R, packet_t>> record_;
    };

    template<class R>
        requires std::execution::receiver_of<R, completion_signatures>
    auto connect(R rcvr) && -> __op<R> {
        return __op<R>{
            std::move(state),
            std::move(session),
            std::move(request),
            std::move(response),
            std::move(handler),
            options,
            std::move(rcvr)};
    }
};

} // namespace __request_detail

class request_session {
public:
    request_session()
        : request_session(device_session{}) {}

    explicit request_session(
        device_session session,
        std::pmr::memory_resource* memory = default_memory_resource())
        : state_(std::allocate_shared<__request_detail::__state>(
              std::pmr::polymorphic_allocator<__request_detail::__state>{
                  normalize_memory_resource(memory)},
              normalize_memory_resource(memory)))
        , session_(std::move(session))
    {}

    [[nodiscard]] auto pending_count() const noexcept -> std::size_t {
        return state_ ? state_->pending_count() : 0;
    }

    [[nodiscard]] auto late_response_count() const noexcept -> std::size_t {
        return state_ ? state_->late_response_count() : 0;
    }

    void wait() noexcept {
        if (state_) {
            state_->wait();
        }
    }

    template<class Request, class Response, class Handler>
    auto submit_request(
        Request request,
        Response response,
        Handler&& handler,
        request_options options = {}) {
        using sender_t = __request_detail::__sender<
            std::decay_t<Request>,
            std::decay_t<Response>,
            std::decay_t<Handler>>;
        return sender_t{
            state_,
            session_,
            std::move(request),
            std::move(response),
            static_cast<Handler&&>(handler),
            options};
    }

    template<class Request, class Response, class Handler>
    auto submit_request_typed(
        Request request,
        Response response,
        Handler&& handler,
        request_options options = {}) {
        using packet_t = request_packet<std::decay_t<Request>, std::decay_t<Response>>;
        return __typed_detail::value_sender<packet_t>(
            submit_request(
                std::move(request),
                std::move(response),
                static_cast<Handler&&>(handler),
                options));
    }

private:
    std::shared_ptr<__request_detail::__state> state_;
    device_session session_;
};

} // namespace forge::accel::mock
