#pragma once

#include <cstddef>
#include <execution>

namespace test_execution::replacement_fixture {

namespace spi = std::execution::parallel_scheduler_replacement;
namespace detail = std::execution::__forge_parsched_detail;

// Stage 1 has no public sender to manufacture a proxy or its operation state.
// Keep construction of the real backport internals at this test-only seam.
using stop_event = detail::__proxy_stop_event;

template<class Receiver>
using stop_reader = detail::__read_proxy_env<Receiver>;

template<class Receiver, class Observer = detail::__no_proxy_stop_observer>
using ordinary_proxy = detail::__receiver_proxy<Receiver, spi::receiver_proxy, Observer>;

template<class Receiver, class Item>
using bulk_proxy = detail::__bulk_item_receiver_proxy<Receiver, Item>;

template<class Proxy, std::size_t Size = 64>
using operation = detail::__backend_operation<Proxy, Size>;

template<class Op>
void start(Op& op) noexcept { op.__start(); }

template<class Op>
void start_chunked(Op& op, std::size_t shape) noexcept { op.__start_chunked(shape); }

template<class Op>
void start_unchunked(Op& op, std::size_t shape) noexcept { op.__start_unchunked(shape); }

struct invalid_query {
    void operator()(std::execution::env<>) const = delete;
};

// The public stop CPO would fall back to never_stop_token. Substitute an
// invalid call only in the typed dispatch, then query the public base.
struct invalid_call_proxy final : spi::receiver_proxy {
    struct receiver {};
    using read = detail::__read_proxy_env<receiver, invalid_query>;
    detail::__proxy_stop_query<read, detail::__no_proxy_stop_observer> query{detail::__no_proxy_stop_observer{}};

    bool __query_env(std::inplace_stop_token& token) const noexcept override {
        auto target = receiver{};
        return query.__query_env({target}, token);
    }

    void set_value() noexcept override {}
    void set_error(std::exception_ptr) noexcept override {}
    void set_stopped() noexcept override {}
};

} // namespace test_execution::replacement_fixture
