#include <gtest/gtest.h>
#include <execution>

#include <functional>
#include <atomic>
#include <barrier>
#include <memory>
#include <optional>
#include <semaphore>
#include <thread>
#include <utility>

namespace {

// Unlike in-place destructor fixtures, this releases storage so ASan can
// diagnose a source access after receiver-driven operation destruction.
struct heap_operation {
    void* object = nullptr;
    void (*destroy)(void*) noexcept = nullptr;
    ~heap_operation() { reset(); }
    void reset() noexcept {
        auto* current = std::exchange(object, nullptr);
        if (current) destroy(current);
    }
    template<class Factory>
    auto emplace(Factory factory) -> decltype(factory())& {
        using op_t = decltype(factory());
        auto* op = new op_t(factory());
        object = op;
        destroy = [](void* ptr) noexcept { delete static_cast<op_t*>(ptr); };
        return *op;
    }
};

struct lifetime_receiver {
    using receiver_concept = std::execution::receiver_t;
    heap_operation* owner;
    int* completions;
    std::inplace_stop_token token;
    int* completion_kind = nullptr;

    void finish() noexcept {
        auto* box = owner;
        ++*completions;
        box->reset();
    }
    template<class... Ts> void set_value(Ts&&...) && noexcept {
        if (completion_kind) *completion_kind = 0;
        finish();
    }
    template<class E> void set_error(E&&) && noexcept {
        if (completion_kind) *completion_kind = 1;
        finish();
    }
    void set_stopped() && noexcept {
        if (completion_kind) *completion_kind = 2;
        finish();
    }
    auto get_env() const noexcept {
        return std::execution::prop(std::execution::get_stop_token, token);
    }
};

struct inline_stop_sender {
    using sender_concept = std::execution::sender_t;
    int channel = 2;
    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<std::execution::set_value_t(int),
            std::execution::set_error_t(int), std::execution::set_stopped_t()> { return {}; }

    template<class R>
    struct op {
        using operation_state_concept = std::execution::operation_state_t;
        R receiver;
        int channel;
        struct notify {
            op* self;
            void operator()() const noexcept {
                auto* current = self;
                current->registration.reset();
                if (current->channel == 0)
                    std::execution::set_value(std::move(current->receiver), 42);
                else if (current->channel == 1)
                    std::execution::set_error(std::move(current->receiver), 42);
                else std::execution::set_stopped(std::move(current->receiver));
            }
        };
        using token_t = decltype(std::execution::get_stop_token(
            std::execution::get_env(std::declval<const R&>())));
        std::optional<std::stop_callback_for_t<token_t, notify>> registration;
        op(R r, int c) : receiver(std::move(r)), channel(c) {}
        void start() & noexcept {
            registration.emplace(std::execution::get_stop_token(
                std::execution::get_env(receiver)), notify{this});
        }
    };
    template<class R> auto connect(R r) const -> op<R> { return op<R>{std::move(r), channel}; }
};

// Signals entry to input deregistration without relying on scheduler delays.
struct observed_token {
    std::inplace_stop_token token;
    std::binary_semaphore* deregistering;
    template<class Fn> struct callback_type {
        std::binary_semaphore* deregistering;
        std::inplace_stop_callback<Fn> registration;
        callback_type(observed_token t, Fn fn)
            : deregistering(t.deregistering), registration(t.token, std::move(fn)) {}
        ~callback_type() { deregistering->release(); }
    };
    bool stop_requested() const noexcept { return token.stop_requested(); }
    bool stop_possible() const noexcept { return token.stop_possible(); }
    bool operator==(const observed_token&) const noexcept = default;
};

struct reference_payload {
    reference_payload() = default;
    reference_payload(const reference_payload&) = delete;
    reference_payload(reference_payload&&) = delete;
};

struct overlap_state {
    std::binary_semaphore entered{0};
    std::binary_semaphore release{0};
    std::binary_semaphore deregistering{0};
    void* operation = nullptr;
    void (*complete)(void*, int, reference_payload&) noexcept = nullptr;
    std::atomic<int> completions{0};
    int channel = -1;
    reference_payload* received = nullptr;
};

struct overlap_sender {
    using sender_concept = std::execution::sender_t;
    overlap_state* state;
    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<
            std::execution::set_value_t(reference_payload&),
            std::execution::set_error_t(reference_payload&),
            std::execution::set_stopped_t()> { return {}; }
    template<class R> struct op {
        using operation_state_concept = std::execution::operation_state_t;
        R receiver;
        overlap_state* state;
        struct block {
            overlap_state* state;
            void operator()() const noexcept {
                auto* local = state;
                local->entered.release();
                local->release.acquire();
            }
        };
        using token_t = std::remove_cvref_t<decltype(std::execution::get_stop_token(
            std::execution::get_env(std::declval<const R&>())))>;
        std::optional<std::stop_callback_for_t<token_t, block>> registration;
        op(R r, overlap_state* s) : receiver(std::move(r)), state(s) {}
        void start() & noexcept {
            registration.emplace(std::execution::get_stop_token(
                std::execution::get_env(receiver)), block{state});
            state->operation = this;
            state->complete = [](void* ptr, int channel, reference_payload& value) noexcept {
                auto* self = static_cast<op*>(ptr);
                // Leave the child's registration intact. The wrapper must wait
                // for its own input callback before the receiver destroys us.
                if (channel == 0) std::execution::set_value(std::move(self->receiver), value);
                else if (channel == 1) std::execution::set_error(std::move(self->receiver), value);
                else std::execution::set_stopped(std::move(self->receiver));
            };
        }
    };
    template<class R> auto connect(R r) const -> op<R> { return op<R>{std::move(r), state}; }
};

struct reference_receiver {
    using receiver_concept = std::execution::receiver_t;
    heap_operation* owner;
    overlap_state* state;
    observed_token token;
    void finish(int channel, reference_payload* value) noexcept {
        auto* result = state;
        result->channel = channel;
        result->received = value;
        auto* box = owner;
        box->reset();
        result->completions.fetch_add(1, std::memory_order_release);
    }
    void set_value(reference_payload& value) && noexcept { finish(0, &value); }
    void set_error(reference_payload& value) && noexcept { finish(1, &value); }
    void set_stopped() && noexcept { finish(2, nullptr); }
    auto get_env() const noexcept {
        return std::execution::prop(std::execution::get_stop_token, token);
    }
};

} // namespace

TEST(StopCallbackLifetimeTest, RunningCallbackCanDestroyItself) {
    std::inplace_stop_source source;
    using callback_t = std::inplace_stop_callback<std::function<void()>>;
    std::unique_ptr<callback_t> callback;
    bool called = false;
    callback = std::make_unique<callback_t>(source.get_token(), [&]() noexcept {
        called = true;
        callback.reset();
    });
    EXPECT_TRUE(source.request_stop());
    EXPECT_TRUE(called);
    EXPECT_FALSE(callback);
}

TEST(StopCallbackLifetimeTest, RequestRacesDeregistrationWithSourceOwnerPresent) {
    for (int iteration = 0; iteration != 64; ++iteration) {
        std::inplace_stop_source source;
        std::atomic<int> calls{0};
        auto notify = [&calls]() noexcept { calls.fetch_add(1, std::memory_order_relaxed); };
        using callback_t = std::inplace_stop_callback<decltype(notify)>;
        auto callback = std::make_unique<callback_t>(source.get_token(), notify);
        std::barrier gate{3};
        std::thread requester{[&] { gate.arrive_and_wait(); source.request_stop(); }};
        std::thread destroyer{[&] { gate.arrive_and_wait(); callback.reset(); }};
        gate.arrive_and_wait();
        requester.join();
        destroyer.join();
        EXPECT_LE(calls.load(), 1);
    }
}

TEST(StopCallbackLifetimeTest, WhenAllOuterStopKeepsSourceThroughChildCompletion) {
    std::inplace_stop_source source;
    heap_operation owner;
    int completions = 0;
    auto& op = owner.emplace([&] {
        return std::execution::connect(
            std::execution::when_all(inline_stop_sender{}, inline_stop_sender{}),
            lifetime_receiver{&owner, &completions, source.get_token()});
    });
    std::execution::start(op);
    source.request_stop();
    EXPECT_EQ(completions, 1);
    EXPECT_EQ(owner.object, nullptr);
}

TEST(StopCallbackLifetimeTest, FutureConsumerStopCanReleaseLastOperationOwner) {
    std::execution::simple_counting_scope scope;
    std::inplace_stop_source source;
    heap_operation owner;
    int completions = 0;
    auto future = std::execution::spawn_future(inline_stop_sender{}, scope.get_token());
    auto& op = owner.emplace([&] {
        return std::execution::connect(std::move(future),
            lifetime_receiver{&owner, &completions, source.get_token()});
    });
    std::execution::start(op);
    source.request_stop();
    EXPECT_EQ(completions, 1);
    EXPECT_EQ(owner.object, nullptr);
    (void)std::this_thread::sync_wait(scope.join());
}

// Known open: production detached controls tolerate this source destruction.
// Run separately with the strict probe under ASan to expose the lifetime gap.
TEST(StopCallbackLifetimeKnownOpenTest, CountingFusionSameThreadReceiverDestroysSource) {
    for (bool scope_input : {false, true}) {
        for (int channel : {0, 1, 2}) {
            std::execution::counting_scope scope;
            std::inplace_stop_source source;
            heap_operation owner;
            int completions = 0;
            int completion_kind = -1;
            auto& op = owner.emplace([&] {
                return std::execution::connect(scope.get_token().wrap(inline_stop_sender{channel}),
                    lifetime_receiver{&owner, &completions, source.get_token(), &completion_kind});
            });
            std::execution::start(op);
            if (scope_input) scope.request_stop();
            else source.request_stop();
            EXPECT_EQ(completions, 1);
            EXPECT_EQ(completion_kind, channel);
            EXPECT_EQ(owner.object, nullptr);
            (void)std::this_thread::sync_wait(scope.join());
        }
    }
}

TEST(StopCallbackLifetimeTest, CountingFusionWaitsForOtherThreadAndForwardsOriginalReference) {
    for (int channel : {0, 1, 2}) {
        std::execution::counting_scope scope;
        std::inplace_stop_source source;
        overlap_state state;
        reference_payload value;
        heap_operation owner;
        auto& operation = owner.emplace([&] {
            return std::execution::connect(scope.get_token().wrap(overlap_sender{&state}),
                reference_receiver{&owner, &state,
                    observed_token{source.get_token(), &state.deregistering}});
        });
        std::execution::start(operation);
        std::thread requester{[&] { source.request_stop(); }};
        state.entered.acquire();
        std::thread completer{[&] { state.complete(state.operation, channel, value); }};
        state.deregistering.acquire();
        EXPECT_EQ(state.completions.load(std::memory_order_acquire), 0);
        state.release.release();
        requester.join();
        completer.join();
        EXPECT_EQ(state.completions.load(), 1);
        EXPECT_EQ(state.channel, channel);
        EXPECT_EQ(state.received, channel == 2 ? nullptr : &value);
        EXPECT_EQ(owner.object, nullptr);
        (void)std::this_thread::sync_wait(scope.join());
    }
}

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define FORGE_STOP_LIFETIME_ASAN 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__)
#define FORGE_STOP_LIFETIME_ASAN 1
#endif

#if defined(FORGE_EXECUTION_TEST_STRICT_STOP_LIFETIME) && defined(FORGE_STOP_LIFETIME_ASAN)
TEST(StopCallbackLifetimeDeathTest, ProbeDetectsSourceDestroyedInsideRequest) {
    // Isolated negative control: this intentionally violates the source contract.
    ASSERT_DEATH({
        auto source = std::make_unique<std::inplace_stop_source>();
        using callback_t = std::inplace_stop_callback<std::function<void()>>;
        std::unique_ptr<callback_t> callback;
        callback = std::make_unique<callback_t>(source->get_token(), [&]() noexcept {
            auto* source_owner = &source;
            callback.reset();
            source_owner->reset();
        });
        source->request_stop();
    }, "[Hh]eap-use-after-free");
}
#endif
