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
                // The child owns registration and waits before forwarding any
                // completion that can destroy the operation on another thread.
                self->registration.reset();
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

using fused_token = std::execution::__forge_counting_scope::__fused_stop_token<
    std::inplace_stop_token>;

struct gated_registration_state {
    bool delay_construction = false;
    bool delay_invocation = false;
    std::binary_semaphore constructing{0};
    std::binary_semaphore construct{0};
    std::binary_semaphore invoking{0};
    std::binary_semaphore invoke{0};
    std::binary_semaphore deregistering{0};
};

// Keep the second input's invocation live before it enters the fused trampoline.
struct gated_token {
    std::inplace_stop_token token;
    gated_registration_state* gate;
    static auto prepare(gated_token t) -> std::inplace_stop_token {
        if (t.gate->delay_construction) {
            t.gate->constructing.release();
            t.gate->construct.acquire();
        }
        return t.token;
    }
    template<class Fn> struct callback_type {
        struct invoke_fn {
            Fn fn;
            gated_registration_state* gate;
            void operator()() noexcept {
                auto* local = gate;
                if (local->delay_invocation) {
                    local->invoking.release();
                    local->invoke.acquire();
                }
                std::forward<Fn>(fn)();
            }
        };
        gated_registration_state* gate;
        std::inplace_stop_callback<invoke_fn> registration;
        callback_type(gated_token t, Fn fn)
            : gate(t.gate), registration(prepare(t), invoke_fn{std::move(fn), gate}) {}
        ~callback_type() { gate->deregistering.release(); }
    };
    bool stop_requested() const noexcept { return token.stop_requested(); }
    bool stop_possible() const noexcept { return token.stop_possible(); }
    bool operator==(const gated_token&) const noexcept = default;
};

struct fused_fn_state {
    std::inplace_stop_source* input;
    std::inplace_stop_source* downstream;
    int calls = 0;
    std::thread::id invoked_on;
    bool stop_on_destruction = false;
};

// Construct from an initializer directly; no temporary callable or copy.
struct rvalue_fused_fn {
    fused_fn_state* state;
    explicit rvalue_fused_fn(fused_fn_state* s) noexcept : state(s) {}
    rvalue_fused_fn(const rvalue_fused_fn&) = delete;
    void operator()() && noexcept {
        auto* local = state;
        ++local->calls;
        local->invoked_on = std::this_thread::get_id();
        // Also exercise a reentrant request on the other input.
        local->input->request_stop();
        local->downstream->request_stop();
    }
    ~rvalue_fused_fn() {
        if (state->stop_on_destruction) {
            state->input->request_stop();
            state->downstream->request_stop();
        }
    }
};

} // namespace

TEST(StopCallbackLifetimeTest, FusedCallbackPrerequestedInputsInvokeOnceOnConstructingThread) {
    for (int stopped_inputs : {1, 2, 3}) {
        std::inplace_stop_source input;
        std::inplace_stop_source downstream;
        if (stopped_inputs & 1) input.request_stop();
        if (stopped_inputs & 2) downstream.request_stop();
        fused_fn_state state{&input, &downstream};
        fused_token::callback_type<rvalue_fused_fn> registration(
            fused_token{input.get_token(), downstream.get_token()}, &state);
        EXPECT_EQ(state.calls, 1);
        EXPECT_EQ(state.invoked_on, std::this_thread::get_id());
    }
}

TEST(StopCallbackLifetimeTest, FusedCallbackDeregistersBothInputsBeforeDestroyingFn) {
    std::inplace_stop_source input;
    std::inplace_stop_source downstream;
    fused_fn_state state{&input, &downstream};
    state.stop_on_destruction = true;
    {
        fused_token::callback_type<rvalue_fused_fn> registration(
            fused_token{input.get_token(), downstream.get_token()}, &state);
    }
    EXPECT_TRUE(input.stop_requested());
    EXPECT_TRUE(downstream.stop_requested());
    EXPECT_EQ(state.calls, 0);
}

TEST(StopCallbackLifetimeTest, FusedCallbackConcurrentRequestsAllowSelfDestruction) {
    for (int iteration = 0; iteration != 64; ++iteration) {
        std::inplace_stop_source input;
        std::inplace_stop_source downstream;
        std::atomic<int> calls{0};
        using registration_t = fused_token::callback_type<std::function<void()>>;
        std::unique_ptr<registration_t> registration;
        registration = std::make_unique<registration_t>(
            fused_token{input.get_token(), downstream.get_token()}, [&]() noexcept {
                auto* counter = &calls;
                auto* owner = &registration;
                counter->fetch_add(1, std::memory_order_relaxed);
                owner->reset();
            });
        std::barrier gate{3};
        std::thread first{[&] { gate.arrive_and_wait(); input.request_stop(); }};
        std::thread second{[&] { gate.arrive_and_wait(); downstream.request_stop(); }};
        gate.arrive_and_wait();
        first.join();
        second.join();
        EXPECT_EQ(calls.load(), 1);
        EXPECT_FALSE(registration);
    }
}

TEST(StopCallbackLifetimeTest, FusedCallbackAliasedInputsAllowSelfAndSiblingDeregistration) {
    std::inplace_stop_source source;
    using registration_t = fused_token::callback_type<std::function<void()>>;
    std::unique_ptr<registration_t> registration;
    int calls = 0;
    registration = std::make_unique<registration_t>(
        fused_token{source.get_token(), source.get_token()}, [&]() noexcept {
            auto* owner = &registration;
            ++calls;
            owner->reset();
        });
    EXPECT_TRUE(source.request_stop());
    EXPECT_EQ(calls, 1);
    EXPECT_FALSE(registration);
}

TEST(StopCallbackLifetimeTest, FusedCallbackStopBetweenInputRegistrationsInvokesOnce) {
    std::inplace_stop_source input;
    std::inplace_stop_source downstream;
    gated_registration_state gate;
    gate.delay_construction = true;
    using token_t = std::execution::__forge_counting_scope::__fused_stop_token<gated_token>;
    std::atomic<int> calls{0};
    std::binary_semaphore entered{0};
    std::binary_semaphore release{0};
    auto fn = [&]() noexcept {
        calls.fetch_add(1, std::memory_order_relaxed);
        entered.release();
        release.acquire();
    };
    using registration_t = token_t::callback_type<decltype(fn)>;
    std::unique_ptr<registration_t> registration;
    std::thread constructor{[&] {
        registration = std::make_unique<registration_t>(
            token_t{input.get_token(), gated_token{downstream.get_token(), &gate}}, fn);
    }};
    gate.constructing.acquire();
    std::thread requester{[&] { input.request_stop(); }};
    entered.acquire();
    downstream.request_stop();
    gate.construct.release();
    constructor.join(); // The second, immediate invocation must lose without waiting.
    EXPECT_EQ(calls.load(), 1);
    release.release();
    requester.join();
    registration.reset();
}

TEST(StopCallbackLifetimeTest, FusedCallbackPrerequestedSecondInputWinsBeforeFirstInputCanRace) {
    std::inplace_stop_source input;
    std::inplace_stop_source downstream;
    downstream.request_stop();
    gated_registration_state gate;
    gate.delay_construction = true;
    using token_t = std::execution::__forge_counting_scope::__fused_stop_token<gated_token>;
    std::atomic<int> calls{0};
    std::thread::id invoked_on;
    auto fn = [&]() noexcept {
        invoked_on = std::this_thread::get_id();
        calls.fetch_add(1, std::memory_order_relaxed);
    };
    using registration_t = token_t::callback_type<decltype(fn)>;
    std::unique_ptr<registration_t> registration;
    std::thread constructor{[&] {
        registration = std::make_unique<registration_t>(
            token_t{input.get_token(), gated_token{downstream.get_token(), &gate}}, fn);
    }};
    const auto constructing_thread = constructor.get_id();
    gate.constructing.acquire();
    input.request_stop();
    gate.construct.release();
    constructor.join();
    EXPECT_EQ(calls.load(), 1);
    EXPECT_EQ(invoked_on, constructing_thread);
}

TEST(StopCallbackLifetimeTest, FusedCallbackSelfDestructionWaitsForLosingInputWithoutDeadlock) {
    std::inplace_stop_source input;
    std::inplace_stop_source downstream;
    gated_registration_state gate;
    gate.delay_invocation = true;
    using token_t = std::execution::__forge_counting_scope::__fused_stop_token<gated_token>;
    using registration_t = token_t::callback_type<std::function<void()>>;
    std::unique_ptr<registration_t> registration;
    std::atomic<bool> destroyed{false};
    std::atomic<int> calls{0};
    registration = std::make_unique<registration_t>(
        token_t{input.get_token(), gated_token{downstream.get_token(), &gate}}, [&]() noexcept {
            auto* owner = &registration;
            auto* finished = &destroyed;
            calls.fetch_add(1, std::memory_order_relaxed);
            owner->reset();
            finished->store(true, std::memory_order_release);
        });
    std::thread loser{[&] { downstream.request_stop(); }};
    gate.invoking.acquire();
    std::thread winner{[&] { input.request_stop(); }};
    gate.deregistering.acquire();
    EXPECT_FALSE(destroyed.load(std::memory_order_acquire));
    gate.invoke.release();
    loser.join();
    winner.join();
    EXPECT_TRUE(destroyed.load());
    EXPECT_EQ(calls.load(), 1);
    EXPECT_FALSE(registration);
}

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

TEST(StopCallbackLifetimeTest, CountingFusionSameThreadReceiverDestroysOperation) {
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
    for (bool scope_input : {false, true}) {
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
            std::thread requester{[&] {
                if (scope_input) scope.request_stop();
                else source.request_stop();
            }};
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
