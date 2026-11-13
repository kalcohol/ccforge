#include <forge/execution_extensions.hpp>
#include <gtest/gtest.h>
#include <execution>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <new>
#include <tuple>
#include <type_traits>
#include <utility>

namespace allocation_probe {
std::atomic<bool> enabled{false};
std::atomic<std::size_t> calls{0};

void count() noexcept {
    if (enabled.load(std::memory_order_relaxed)) {
        calls.fetch_add(1, std::memory_order_relaxed);
    }
}

void* allocate(std::size_t size) {
    count();
    for (;;) {
        if (void* p = std::malloc(size == 0 ? 1 : size)) return p;
        if (auto handler = std::get_new_handler()) handler();
        else throw std::bad_alloc{};
    }
}

// Use malloc/free for both paths on every platform, including Windows. The
// original allocation is retained immediately before the aligned address.
void* allocate_aligned(std::size_t size, std::size_t alignment) {
    if (alignment < alignof(void*)) alignment = alignof(void*);
    const auto extra = alignment - 1 + sizeof(void*);
    if (size > (std::numeric_limits<std::size_t>::max)() - extra) {
        count();
        throw std::bad_alloc{};
    }
    void* raw = allocate(size + extra);
    auto address = reinterpret_cast<std::uintptr_t>(raw) + sizeof(void*);
    address = (address + alignment - 1) & ~(alignment - 1);
    void* aligned = reinterpret_cast<void*>(address);
    static_cast<void**>(aligned)[-1] = raw;
    return aligned;
}

void free_aligned(void* p) noexcept {
    if (p) std::free(static_cast<void**>(p)[-1]);
}

struct window {
    window() noexcept {
        calls.store(0, std::memory_order_relaxed);
        enabled.store(true, std::memory_order_relaxed);
    }
    ~window() { enabled.store(false, std::memory_order_relaxed); }
};

template<class F>
std::size_t measure(F&& f) {
    {
        window scope;
        std::forward<F>(f)();
    }
    return calls.load(std::memory_order_relaxed);
}
} // namespace allocation_probe

// Keep these replacements confined to this executable. No googletest calls
// occur while a measurement window is open.
void* operator new(std::size_t n) { return allocation_probe::allocate(n); }
void* operator new[](std::size_t n) { return allocation_probe::allocate(n); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
    try { return allocation_probe::allocate(n); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept {
    try { return allocation_probe::allocate(n); } catch (...) { return nullptr; }
}
void* operator new(std::size_t n, std::align_val_t a) {
    return allocation_probe::allocate_aligned(n, static_cast<std::size_t>(a));
}
void* operator new[](std::size_t n, std::align_val_t a) {
    return allocation_probe::allocate_aligned(n, static_cast<std::size_t>(a));
}
void* operator new(std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept {
    try { return allocation_probe::allocate_aligned(n, static_cast<std::size_t>(a)); }
    catch (...) { return nullptr; }
}
void* operator new[](std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept {
    try { return allocation_probe::allocate_aligned(n, static_cast<std::size_t>(a)); }
    catch (...) { return nullptr; }
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete(void* p, std::align_val_t) noexcept { allocation_probe::free_aligned(p); }
void operator delete[](void* p, std::align_val_t) noexcept { allocation_probe::free_aligned(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept {
    allocation_probe::free_aligned(p);
}
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept {
    allocation_probe::free_aligned(p);
}
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept {
    allocation_probe::free_aligned(p);
}
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept {
    allocation_probe::free_aligned(p);
}

namespace {
namespace ex = std::execution;

struct calibration_callback {
    // Match the pointer-sized when_all callback without doing any work.
    void* state = nullptr;
    void operator()() const noexcept {}
};

std::size_t allocations_per_registration() {
    std::inplace_stop_source source;
    return allocation_probe::measure([&] {
        std::inplace_stop_callback<calibration_callback> callback(
            source.get_token(), calibration_callback{});
    });
}

struct result {
    int completions = 0;
    int sum = 0;
    int errors = 0;
    int stopped = 0;
    bool callback_present_at_completion = false;
};

struct receiver {
    using receiver_concept = ex::receiver_t;
    result* out;

    template<class... Vs>
    void set_value(Vs... vs) && noexcept {
        ((out->sum += vs), ...);
        ++out->completions;
    }
    template<class E>
    void set_error(E&&) && noexcept { ++out->completions; ++out->errors; }
    void set_stopped() && noexcept { ++out->completions; ++out->stopped; }
    auto get_env() const noexcept -> forge::empty_env { return {}; }
};

template<std::size_t Bytes, std::size_t Alignment = alignof(std::max_align_t)>
struct padded_sender {
    using sender_concept = ex::sender_t;
    using completion_signatures = ex::completion_signatures<ex::set_value_t(int)>;
    template<class Self, class Env>
    static auto get_completion_signatures() noexcept -> completion_signatures { return {}; }
    int value;

    template<class R>
    struct alignas(Alignment) op : ex::__forge_detail::__immovable {
        using operation_state_concept = ex::operation_state_t;
        R recv;
        int value;
        std::byte padding[Bytes];

        op(R r, int v) noexcept(std::is_nothrow_move_constructible_v<R>)
            : recv(std::move(r)), value(v) {}
        void start() & noexcept {
            const bool aligned = reinterpret_cast<std::uintptr_t>(this) % Alignment == 0;
            ex::set_value(std::move(recv), aligned ? value : -1000);
        }
    };

    template<class R>
    auto connect(R r) const noexcept(std::is_nothrow_move_constructible_v<R>) -> op<R> {
        return op<R>{std::move(r), value};
    }
    auto get_env() const noexcept -> forge::empty_env { return {}; }
};

static_assert(sizeof(padded_sender<8192>::op<receiver>) > 4096);
static_assert(alignof(padded_sender<32, 64>::op<receiver>) >= 64);

struct large_stop_token {
    bool* requested;
    int* live_callbacks;

    template<class F>
    struct alignas(64) callback_type {
        std::byte padding[1024];
        F callback;
        int* live_callbacks;

        callback_type(large_stop_token t, F f) noexcept
            : callback(std::move(f)), live_callbacks(t.live_callbacks) {
            ++*live_callbacks;
            if (t.stop_requested()) callback();
        }
        callback_type(const callback_type&) = delete;
        callback_type(callback_type&&) = delete;
        ~callback_type() { --*live_callbacks; }
    };

    bool stop_requested() const noexcept { return *requested; }
    bool stop_possible() const noexcept { return true; }
    bool operator==(const large_stop_token&) const noexcept = default;
};

template<class Token>
struct token_env {
    Token token;
    auto query(ex::get_stop_token_t) const noexcept { return token; }
};

template<class Token>
struct token_receiver : receiver {
    Token token;
    template<class... Vs>
    void set_value(Vs... vs) && noexcept {
        if constexpr (requires { token.live_callbacks; }) {
            out->callback_present_at_completion = *token.live_callbacks != 0;
        }
        std::move(static_cast<receiver&>(*this)).set_value(vs...);
    }
    auto get_env() const noexcept { return token_env<Token>{token}; }
};

template<class S, class Token = std::never_stop_token>
void check_inline(S sender, int expected, std::size_t registrations, Token token = {}) {
    const auto per_registration = allocations_per_registration();
    result out;
    const auto calls = allocation_probe::measure([&] {
        auto op = ex::connect(std::move(sender), token_receiver<Token>{{&out}, token});
        static_assert(noexcept(ex::start(op)));
        ex::start(op);
    });
    // Callers count registrations on inplace_stop_token only. Zero registrations
    // require exactly zero allocations, independent of the calibrated cost.
    EXPECT_EQ(calls, registrations * per_registration);
    EXPECT_EQ(out.completions, 1);
    EXPECT_EQ(out.sum, expected);
    EXPECT_EQ(out.errors, 0);
    EXPECT_EQ(out.stopped, 0);
}

TEST(ExecutionStaticOpstateAllocations, PositiveControl) {
    const auto calls = allocation_probe::measure([] {
        // A direct call cannot be elided as a new-expression can be.
        void* p = ::operator new(37);
        ::operator delete(p);
    });
    EXPECT_EQ(calls, 1u);
}

TEST(ExecutionStaticOpstateAllocations, NestedLetValueTwoAndThreeLevels) {
    // 0 registrations: these let_value chains contain no when_all.
    check_inline(ex::let_value(ex::just(1), [](int& v) {
        return ex::let_value(ex::just(v + 1), [](int& w) { return ex::just(w + 1); });
    }), 3, 0);
    // 0 registrations: no when_all, even with a large over-aligned successor.
    check_inline(ex::let_value(ex::just(1), [](int& v) {
        return ex::let_value(ex::just(v + 1), [](int& w) {
            return ex::let_value(ex::just(w + 1), [](int& x) noexcept {
                return padded_sender<8192, 64>{x + 1};
            });
        });
    }), 4, 0);
}

TEST(ExecutionStaticOpstateAllocations, LetErrorAndLetStopped) {
    // 0 registrations: let_error and let_stopped do not register callbacks.
    check_inline(ex::let_error(ex::just_error(7), [](int& e) {
        return ex::let_stopped(ex::just_stopped(), [e] { return padded_sender<8192>{e}; });
    }), 7, 0);
    // 0 registrations: the reversed let channel composition also has no when_all.
    check_inline(ex::let_stopped(ex::just_stopped(), [] {
        return ex::let_error(ex::just_error(9), [](int& e) { return ex::just(e); });
    }), 9, 0);
}

TEST(ExecutionStaticOpstateAllocations, LargePredecessorAndArguments) {
    struct big_argument { char bytes[8192]; int value; };
    big_argument arg{};
    arg.value = 42;
    // 0 registrations: storing large arguments adds no stop callback.
    check_inline(ex::let_value(ex::just(arg), [](big_argument& a) {
        return ex::just(a.value);
    }), 42, 0);
    // 0 registrations: the large predecessor has no callback registration.
    check_inline(ex::let_value(padded_sender<8192, 64>{41}, [](int& v) {
        return ex::just(v + 1);
    }), 42, 0);
}

TEST(ExecutionStaticOpstateAllocations, WhenAllLargeAndOverAlignedChildren) {
    // 0 registrations: the outer receiver has never_stop_token, and none of
    // these children registers a callback on when_all's child stop source.
    check_inline(ex::when_all(ex::just(1), padded_sender<8192>{2},
        padded_sender<32, 64>{3}, ex::just(4)), 10, 0);
}

TEST(ExecutionStaticOpstateAllocations, WhenAllOfLetAndLetOfWhenAll) {
    // 0 registrations: the outer when_all sees never_stop_token; its let
    // children forward that when_all's token but never register on it.
    check_inline(ex::when_all(
        ex::let_value(ex::just(1), [](int& v) { return ex::just(v); }),
        ex::let_error(ex::just_error(2), [](int& e) { return padded_sender<8192>{e}; }),
        ex::let_stopped(ex::just_stopped(), [] { return padded_sender<32, 64>{3}; })), 6, 0);
    // 0 registrations: let forwards never_stop_token to its when_all successor.
    check_inline(ex::let_value(ex::just(1), [](int& v) {
        return ex::when_all(ex::just(v), padded_sender<8192>{2}, padded_sender<32, 64>{3});
    }), 6, 0);
}

TEST(ExecutionStaticOpstateAllocations, NestedWhenAllRegistrationCounts) {
    // 1 registration: the inner when_all registers on the outer when_all's
    // stop source. The top-level receiver has never_stop_token.
    check_inline(ex::when_all(ex::just(1), ex::when_all(
        padded_sender<8192>{2}, padded_sender<32, 64>{3})), 6, 1);

    // 3 registrations: two sibling inner when_alls register on the top-level
    // stop source, and the deepest when_all registers on its parent's source.
    // The top-level when_all itself sees never_stop_token and skips registration.
    check_inline(ex::when_all(
        ex::when_all(ex::just(1), ex::when_all(ex::just(2), ex::just(3))),
        ex::when_all(ex::just(4), padded_sender<8192, 64>{5})), 15, 3);

    // 1 registration: let forwards the outer when_all's stoppable child token
    // to its when_all successor; only that inner when_all registers.
    check_inline(ex::when_all(ex::just(1), ex::let_value(ex::just(2), [](int& v) {
        return ex::when_all(ex::just(v), padded_sender<8192, 64>{3});
    })), 6, 1);
}

TEST(ExecutionStaticOpstateAllocations, ReceiverTokenControlsTopLevelRegistration) {
    std::inplace_stop_source source;
    // 1 registration: a top-level when_all registers on the receiver's
    // stoppable inplace_stop_token; its simple children register nothing.
    check_inline(ex::when_all(ex::just(1), padded_sender<8192, 64>{2}),
        3, 1, source.get_token());

    // 2 registrations: the top-level when_all registers on the receiver token,
    // and its inner when_all registers on the top-level stop source.
    check_inline(ex::when_all(ex::just(1), ex::when_all(
        ex::just(2), padded_sender<8192, 64>{3})), 6, 2, source.get_token());

    // 0 registrations: a default inplace_stop_token has stop_possible()==false;
    // the top-level when_all skips it and simple children add no registrations.
    check_inline(ex::when_all(ex::just(1), padded_sender<8192, 64>{2}),
        3, 0, std::inplace_stop_token{});
}

TEST(ExecutionStaticOpstateAllocations, LargeOverAlignedStopCallbackBeforeChildStart) {
    // The library's inplace_stop_callback has its own control-block allocation.
    // Use a non-allocating token to isolate when_all's callback storage.
    bool requested = false;
    int live_callbacks = 0;
    const large_stop_token token{&requested, &live_callbacks};
    const auto per_registration = allocation_probe::measure([&] {
        large_stop_token::callback_type<calibration_callback> callback(
            token, calibration_callback{});
    });
    ASSERT_EQ(per_registration, 0u);
    auto observes_stop = [] {
        return ex::read_env([](const auto& env) noexcept {
            return ex::get_stop_token(env).stop_requested() ? 1 : 0;
        });
    };
    auto sender = ex::when_all(observes_stop(), observes_stop());
    result out;
    const auto calls = allocation_probe::measure([&] {
        auto op = ex::connect(std::move(sender),
            token_receiver<large_stop_token>{{&out}, token});
        requested = true;
        ex::start(op);
    });
    // 1 registration on large_stop_token at its separately calibrated zero
    // cost; no child registers on the internal inplace_stop_token.
    EXPECT_EQ(calls, 1u * per_registration);
    EXPECT_EQ(out.completions, 1);
    EXPECT_EQ(out.sum, 2);
    EXPECT_EQ(out.errors, 0);
    EXPECT_EQ(out.stopped, 0);
    EXPECT_EQ(live_callbacks, 0);
    EXPECT_FALSE(out.callback_present_at_completion);
}

TEST(ExecutionStaticOpstateAllocations, SyncWaitDrivesRunLoopCompositions) {
    // Obtain sync_wait's own run_loop scheduler, so no worker/thread creation
    // or unrelated adaptor storage enters the measurement.
    auto sender = ex::let_value(ex::read_env(ex::get_scheduler), [](auto& scheduler) {
        return ex::when_all(
            ex::let_value(ex::schedule(scheduler), [] {
                return ex::let_value(ex::just(1), [](int& v) { return ex::just(v); });
            }),
            ex::let_value(ex::schedule(scheduler), [] { return padded_sender<8192, 64>{2}; }));
    });
    bool completed = false;
    int sum = 0;
    const auto calls = allocation_probe::measure([&] {
        auto values = forge::sync_wait(std::move(sender));
        completed = values.has_value();
        if (values) sum = std::get<0>(*values) + std::get<1>(*values);
    });
    // 0 registrations: sync_wait has no stoppable receiver token; let forwards
    // that environment to when_all, and schedule/let children add no callbacks.
    EXPECT_EQ(calls, 0u);
    EXPECT_TRUE(completed);
    EXPECT_EQ(sum, 3);
}

TEST(ExecutionStaticOpstateAllocations, SyncWaitDrivesNestedWhenAll) {
    const auto per_registration = allocations_per_registration();
    auto sender = ex::let_value(ex::read_env(ex::get_scheduler), [](auto& scheduler) {
        return ex::when_all(
            ex::let_value(ex::schedule(scheduler), [] { return ex::just(1); }),
            ex::when_all(
                ex::let_value(ex::schedule(scheduler), [] { return padded_sender<8192>{2}; }),
                ex::let_value(ex::schedule(scheduler), [] { return padded_sender<32, 64>{3}; })));
    });
    bool completed = false;
    int sum = 0;
    const auto calls = allocation_probe::measure([&] {
        auto values = forge::sync_wait(std::move(sender));
        completed = values.has_value();
        if (values) sum = std::get<0>(*values) + std::get<1>(*values) + std::get<2>(*values);
    });
    // 1 registration: the inner when_all registers on the outer when_all's
    // stop source. sync_wait supplies no stoppable token to the outer when_all.
    EXPECT_EQ(calls, 1u * per_registration);
    EXPECT_TRUE(completed);
    EXPECT_EQ(sum, 6);
}
} // namespace
