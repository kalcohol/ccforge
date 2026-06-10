#include <gtest/gtest.h>
#include <forge/channel.hpp>
#include "forge_counting_resource.hpp"
#include "forge_operation_destroy.hpp"
#include <execution>
#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <thread>
#include <tuple>
#include <vector>

namespace {

struct send_state {
    bool value = false;
    bool stopped = false;
};

struct send_receiver {
    using receiver_concept = std::execution::receiver_t;

    std::shared_ptr<send_state> state;

    void set_value() && noexcept { state->value = true; }
    void set_stopped() && noexcept { state->stopped = true; }
    template<class E>
    void set_error(E&&) && noexcept {}
    auto get_env() const noexcept -> std::execution::empty_env { return {}; }
};

template<class T>
struct recv_state {
    std::optional<T> value;
    bool stopped = false;
};

template<class T>
struct recv_receiver {
    using receiver_concept = std::execution::receiver_t;

    std::shared_ptr<recv_state<T>> state;

    void set_value(T value) && noexcept { state->value.emplace(std::move(value)); }
    void set_stopped() && noexcept { state->stopped = true; }
    template<class E>
    void set_error(E&&) && noexcept {}
    auto get_env() const noexcept -> std::execution::empty_env { return {}; }
};

struct stop_env {
    std::inplace_stop_source* source;

    friend auto tag_invoke(
        std::execution::get_stop_token_t,
        const stop_env& self) noexcept -> std::inplace_stop_token {
        return self.source->get_token();
    }
};

template<class T>
struct stopped_recv_receiver {
    using receiver_concept = std::execution::receiver_t;

    std::shared_ptr<recv_state<T>> state;
    std::inplace_stop_source* source;

    void set_value(T value) && noexcept { state->value.emplace(std::move(value)); }
    void set_stopped() && noexcept { state->stopped = true; }
    template<class E>
    void set_error(E&&) && noexcept {}
    auto get_env() const noexcept -> stop_env { return stop_env{source}; }
};

struct stopped_send_receiver {
    using receiver_concept = std::execution::receiver_t;

    std::shared_ptr<send_state> state;
    std::inplace_stop_source* source;

    void set_value() && noexcept { state->value = true; }
    void set_stopped() && noexcept { state->stopped = true; }
    template<class E>
    void set_error(E&&) && noexcept {}
    auto get_env() const noexcept -> stop_env { return stop_env{source}; }
};

struct reenter_state {
    bool received = false;
    bool sent = false;
};

struct reenter_receiver {
    using receiver_concept = std::execution::receiver_t;

    forge::bounded_channel<int>* channel;
    std::shared_ptr<reenter_state> state;

    void set_value(int value) && noexcept {
        state->received = (value == 1);
        state->sent = channel->try_send(2);
    }

    void set_stopped() && noexcept {}
    template<class E>
    void set_error(E&&) && noexcept {}
    auto get_env() const noexcept -> std::execution::empty_env { return {}; }
};

struct self_destroying_recv_receiver {
    using receiver_concept = std::execution::receiver_t;

    std::inplace_stop_source* source = nullptr;
    forge_test::destroy_context_base* context = nullptr;

    void set_value(int) && noexcept {}
    void set_stopped() && noexcept { context->destroy(); }
    template<class E>
    void set_error(E&&) && noexcept { context->destroy(); }
    auto get_env() const noexcept -> stop_env { return stop_env{source}; }
};

} // namespace

TEST(ChannelTest, SendThenRecvDeliversValue) {
    forge::bounded_channel<int> channel{1};

    auto sent = std::execution::sync_wait(channel.async_send(42));
    auto received = std::execution::sync_wait(channel.async_recv());

    ASSERT_TRUE(sent.has_value());
    ASSERT_TRUE(received.has_value());
    EXPECT_EQ(std::get<0>(*received), 42);
}

TEST(ChannelTest, OptionsConstructorUsesCustomMemoryResource) {
    forge_test::counting_resource resource;

    {
        forge::bounded_channel<int> channel{forge::bounded_channel_options{
            .capacity = 1,
            .memory = &resource,
        }};
        EXPECT_EQ(channel.capacity(), 1u);
        EXPECT_GT(resource.allocations(), 0u);

        ASSERT_TRUE(std::execution::sync_wait(channel.async_send(1)).has_value());

        auto send_state_ptr = std::make_shared<send_state>();
        auto pending = channel.async_send(2);
        auto send_op = std::execution::connect(
            std::move(pending),
            send_receiver{send_state_ptr});
        std::execution::start(send_op);

        EXPECT_FALSE(send_state_ptr->value);
        EXPECT_GT(resource.allocations(), 0u);

        auto first = std::execution::sync_wait(channel.async_recv());
        ASSERT_TRUE(first.has_value());
        EXPECT_EQ(std::get<0>(*first), 1);
        EXPECT_TRUE(send_state_ptr->value);
    }

    EXPECT_EQ(resource.allocations(), resource.deallocations());
}

TEST(ChannelTest, RecvThenSendDirectlyHandsOffValue) {
    forge::bounded_channel<int> channel{1};
    auto state = std::make_shared<recv_state<int>>();

    auto recv = channel.async_recv();
    auto op = std::execution::connect(std::move(recv), recv_receiver<int>{state});
    std::execution::start(op);

    EXPECT_FALSE(state->value.has_value());
    auto sent = std::execution::sync_wait(channel.async_send(7));

    ASSERT_TRUE(sent.has_value());
    ASSERT_TRUE(state->value.has_value());
    EXPECT_EQ(*state->value, 7);
}

TEST(ChannelTest, BoundedCapacityBackpressuresPendingSend) {
    forge::bounded_channel<int> channel{1};
    ASSERT_TRUE(std::execution::sync_wait(channel.async_send(1)).has_value());

    auto send_state_ptr = std::make_shared<send_state>();
    auto pending = channel.async_send(2);
    auto send_op = std::execution::connect(std::move(pending), send_receiver{send_state_ptr});
    std::execution::start(send_op);

    EXPECT_FALSE(send_state_ptr->value);

    auto first = std::execution::sync_wait(channel.async_recv());
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(std::get<0>(*first), 1);
    EXPECT_TRUE(send_state_ptr->value);

    auto second = std::execution::sync_wait(channel.async_recv());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(std::get<0>(*second), 2);
}

TEST(ChannelTest, CloseRejectsFutureSendsAndDrainsBufferedValues) {
    forge::bounded_channel<int> channel{2};
    ASSERT_TRUE(std::execution::sync_wait(channel.async_send(1)).has_value());
    ASSERT_TRUE(std::execution::sync_wait(channel.async_send(2)).has_value());

    channel.close();

    EXPECT_FALSE(std::execution::sync_wait(channel.async_send(3)).has_value());

    auto first = std::execution::sync_wait(channel.async_recv());
    auto second = std::execution::sync_wait(channel.async_recv());
    auto stopped = std::execution::sync_wait(channel.async_recv());

    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(std::get<0>(*first), 1);
    EXPECT_EQ(std::get<0>(*second), 2);
    EXPECT_FALSE(stopped.has_value());
}

TEST(ChannelTest, CloseCompletesPendingSendStopped) {
    forge::bounded_channel<int> channel{0};
    auto state = std::make_shared<send_state>();

    auto pending = channel.async_send(1);
    auto op = std::execution::connect(std::move(pending), send_receiver{state});
    std::execution::start(op);

    EXPECT_FALSE(state->value);
    EXPECT_FALSE(state->stopped);

    channel.close();

    EXPECT_FALSE(state->value);
    EXPECT_TRUE(state->stopped);
}

TEST(ChannelTest, CloseDrainsBufferedValueThenStopsPendingReceivers) {
    forge::bounded_channel<int> channel{1};
    ASSERT_TRUE(std::execution::sync_wait(channel.async_send(9)).has_value());

    auto first = std::make_shared<recv_state<int>>();
    auto second = std::make_shared<recv_state<int>>();
    auto first_recv = channel.async_recv();
    auto second_recv = channel.async_recv();
    auto first_op = std::execution::connect(
        std::move(first_recv),
        recv_receiver<int>{first});
    auto second_op = std::execution::connect(
        std::move(second_recv),
        recv_receiver<int>{second});
    std::execution::start(first_op);
    std::execution::start(second_op);

    ASSERT_TRUE(first->value.has_value());
    EXPECT_EQ(*first->value, 9);
    EXPECT_FALSE(second->stopped);

    channel.close();

    EXPECT_TRUE(second->stopped);
    EXPECT_FALSE(channel.try_recv().has_value());
}

TEST(ChannelTest, RequestStopCancelsPendingOperationsAndDiscardsBuffer) {
    forge::bounded_channel<int> send_channel{0};
    auto send_state_ptr = std::make_shared<send_state>();
    auto send = send_channel.async_send(1);
    auto send_op = std::execution::connect(std::move(send), send_receiver{send_state_ptr});
    std::execution::start(send_op);

    send_channel.request_stop();
    EXPECT_TRUE(send_state_ptr->stopped);

    forge::bounded_channel<int> pending_recv_channel{1};
    auto pending_recv_state = std::make_shared<recv_state<int>>();
    auto pending_recv = pending_recv_channel.async_recv();
    auto pending_recv_op = std::execution::connect(
        std::move(pending_recv),
        recv_receiver<int>{pending_recv_state});
    std::execution::start(pending_recv_op);

    EXPECT_FALSE(pending_recv_state->stopped);
    pending_recv_channel.request_stop();
    EXPECT_TRUE(pending_recv_state->stopped);

    forge::bounded_channel<int> recv_channel{1};
    ASSERT_TRUE(std::execution::sync_wait(recv_channel.async_send(9)).has_value());
    auto recv_state_ptr = std::make_shared<recv_state<int>>();
    auto recv = recv_channel.async_recv();
    auto recv_op = std::execution::connect(std::move(recv), recv_receiver<int>{recv_state_ptr});
    recv_channel.request_stop();
    std::execution::start(recv_op);

    EXPECT_TRUE(recv_state_ptr->stopped);
    EXPECT_FALSE(recv_channel.try_recv().has_value());
}

TEST(ChannelTest, MoveOnlyValuesWork) {
    forge::bounded_channel<std::unique_ptr<int>> channel{1};

    ASSERT_TRUE(std::execution::sync_wait(
        channel.async_send(std::make_unique<int>(33))).has_value());
    auto received = std::execution::sync_wait(channel.async_recv());

    ASSERT_TRUE(received.has_value());
    auto ptr = std::move(std::get<0>(*received));
    ASSERT_TRUE(ptr);
    EXPECT_EQ(*ptr, 33);
}

TEST(ChannelTest, PreStartStopTokenCompletesStoppedWithoutEnqueue) {
    forge::bounded_channel<int> channel{1};
    std::inplace_stop_source source;
    source.request_stop();

    auto recv_state_ptr = std::make_shared<recv_state<int>>();
    auto recv = channel.async_recv();
    auto recv_op = std::execution::connect(
        std::move(recv),
        stopped_recv_receiver<int>{recv_state_ptr, &source});
    std::execution::start(recv_op);

    EXPECT_TRUE(recv_state_ptr->stopped);
    EXPECT_TRUE(channel.try_send(4));

    auto send_state_ptr = std::make_shared<send_state>();
    auto send = channel.async_send(5);
    auto send_op = std::execution::connect(
        std::move(send),
        stopped_send_receiver{send_state_ptr, &source});
    std::execution::start(send_op);

    EXPECT_TRUE(send_state_ptr->stopped);
}

TEST(ChannelTest, PreStartStopTokenAllowsReceiverToDestroyOperation) {
    forge::bounded_channel<int> channel{1};
    std::inplace_stop_source source;
    source.request_stop();

    using sender_t = decltype(channel.async_recv());
    using receiver_t = self_destroying_recv_receiver;
    using op_t = decltype(std::execution::connect(
        std::declval<sender_t>(),
        std::declval<receiver_t>()));

    bool destroyed = false;
    forge_test::operation_destroy_context<op_t> context{&destroyed};

    auto& op = context.emplace_from([&] {
        return std::execution::connect(
            channel.async_recv(),
            self_destroying_recv_receiver{&source, &context});
    });
    std::execution::start(op);

    EXPECT_TRUE(destroyed);
    EXPECT_FALSE(context.has_value);
}

TEST(ChannelTest, PostEnqueueReceiverStopCompletesPendingRecv) {
    forge::bounded_channel<int> channel{1};
    std::inplace_stop_source source;
    auto state = std::make_shared<recv_state<int>>();

    auto recv = channel.async_recv();
    auto recv_op = std::execution::connect(
        std::move(recv),
        stopped_recv_receiver<int>{state, &source});
    std::execution::start(recv_op);

    source.request_stop();
    EXPECT_TRUE(state->stopped);
    EXPECT_TRUE(channel.try_send(4));

    auto value = channel.try_recv();
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, 4);
}

TEST(ChannelTest, PostEnqueueReceiverStopCompletesPendingSend) {
    forge::bounded_channel<int> channel{1};
    ASSERT_TRUE(std::execution::sync_wait(channel.async_send(1)).has_value());

    std::inplace_stop_source source;
    auto state = std::make_shared<send_state>();

    auto send = channel.async_send(2);
    auto send_op = std::execution::connect(
        std::move(send),
        stopped_send_receiver{state, &source});
    std::execution::start(send_op);

    EXPECT_FALSE(state->stopped);
    source.request_stop();
    EXPECT_TRUE(state->stopped);
    EXPECT_FALSE(state->value);

    auto first = channel.try_recv();
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(*first, 1);
    EXPECT_FALSE(channel.try_recv().has_value());
}

TEST(ChannelTest, CompletionDoesNotHoldChannelMutex) {
    forge::bounded_channel<int> channel{1};
    auto state = std::make_shared<reenter_state>();

    auto recv = channel.async_recv();
    auto recv_op = std::execution::connect(
        std::move(recv),
        reenter_receiver{&channel, state});
    std::execution::start(recv_op);

    ASSERT_TRUE(std::execution::sync_wait(channel.async_send(1)).has_value());
    EXPECT_TRUE(state->received);
    EXPECT_TRUE(state->sent);

    auto second = channel.try_recv();
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(*second, 2);
}

TEST(ChannelTest, CrossThreadCompletionReleasesStopCallbackBeforeReceiverReturns) {
    forge::bounded_channel<int> channel{0};
    std::atomic<bool> waiter_started{false};
    std::atomic<bool> waiter_done{false};
    std::atomic<bool> got_value{false};

    std::thread waiter{[&] {
        waiter_started.store(true, std::memory_order_release);
        auto result = std::execution::sync_wait(channel.async_recv());
        got_value.store(
            result.has_value() && std::get<0>(*result) == 7,
            std::memory_order_release);
        waiter_done.store(true, std::memory_order_release);
    }};

    while (!waiter_started.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }

    bool sent = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (!sent && std::chrono::steady_clock::now() < deadline) {
        sent = channel.try_send(7);
        if (!sent) {
            std::this_thread::yield();
        }
    }

    if (!sent) {
        channel.request_stop();
        waiter.join();
        FAIL() << "receiver did not become pending before timeout";
    }

    waiter.join();
    EXPECT_TRUE(waiter_done.load(std::memory_order_acquire));
    EXPECT_TRUE(got_value.load(std::memory_order_acquire));
}

TEST(ChannelTest, TrySendTryRecv) {
    forge::bounded_channel<int> channel{1};

    EXPECT_TRUE(channel.try_send(10));
    EXPECT_FALSE(channel.try_send(11));

    auto value = channel.try_recv();
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, 10);
    EXPECT_FALSE(channel.try_recv().has_value());
}

TEST(ChannelTest, ConcurrentProducersConsumers) {
    forge::bounded_channel<int> channel{8};
    std::atomic<int> sum{0};
    std::vector<std::thread> threads;

    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&, i] {
            for (int j = 0; j < 25; ++j) {
                while (!channel.try_send(i * 25 + j)) {
                    std::this_thread::yield();
                }
            }
        });
    }

    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&] {
            for (int j = 0; j < 25; ++j) {
                std::optional<int> value;
                while (!(value = channel.try_recv())) {
                    std::this_thread::yield();
                }
                sum.fetch_add(*value, std::memory_order_acq_rel);
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(sum.load(std::memory_order_acquire), 4950);
}
