#include <gtest/gtest.h>

#include <execution>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>

namespace {

struct env_without_stop_query {};

struct member_stop_env {
    std::inplace_stop_token token;

    auto query(std::get_stop_token_t) const noexcept
        -> std::inplace_stop_token {
        return token;
    }
};

struct qualified_stop_callback {
    int* lvalue_calls;
    int* rvalue_calls;

    void operator()() & noexcept { ++*lvalue_calls; }
    void operator()() && noexcept { ++*rvalue_calls; }
};

struct rvalue_stop_callback {
    int* calls;
    void operator()() && noexcept { ++*calls; }
};

struct static_unstoppable_token {
    template<class Callback>
    struct callback_type {
        template<class Initializer>
        explicit callback_type(const static_unstoppable_token&, Initializer&&) noexcept {}
    };

    static constexpr bool stop_requested() noexcept { return false; }
    static constexpr bool stop_possible() noexcept { return false; }
    bool operator==(const static_unstoppable_token&) const noexcept = default;
};

struct nonstatic_constexpr_false_token : static_unstoppable_token {
    explicit constexpr nonstatic_constexpr_false_token(int) noexcept {}
    constexpr bool stop_possible() const noexcept { return false; }
};

struct nonstatic_constexpr_true_token : static_unstoppable_token {
    constexpr bool stop_possible() const noexcept { return true; }
};

struct static_constexpr_true_token : static_unstoppable_token {
    static constexpr bool stop_possible() noexcept { return true; }
};

struct nonstatic_runtime_token : static_unstoppable_token {
    bool stop_possible() const noexcept { return false; }
};

struct static_runtime_token : static_unstoppable_token {
    static bool stop_possible() noexcept { return false; }
};

struct stateful_stop_token {
    const std::inplace_stop_source* source = nullptr;

    template<class Callback>
    using callback_type = std::inplace_stop_callback<Callback>;

    constexpr bool stop_possible() const noexcept { return source != nullptr; }
    bool stop_requested() const noexcept { return source && source->stop_requested(); }
    operator std::inplace_stop_token() const noexcept {
        return source ? source->get_token() : std::inplace_stop_token{};
    }
    bool operator==(const stateful_stop_token&) const noexcept = default;
};

} // namespace

TEST(ExecutionStopTokenTest, InplaceStopCallbackIsInvoked) {
    std::inplace_stop_source src;
    auto token = src.get_token();

    bool called = false;
    std::inplace_stop_callback cb(token, [&] { called = true; });

    EXPECT_FALSE(called);
    EXPECT_TRUE(src.request_stop());
    EXPECT_TRUE(called);
}

TEST(ExecutionStopTokenTest, InplaceStopRequestIsIdempotent) {
    std::inplace_stop_source src;
    EXPECT_TRUE(src.request_stop());
    EXPECT_FALSE(src.request_stop());
}

TEST(ExecutionStopTokenTest, ConstSourceCanRegisterAndRemoveCallbacks) {
    const std::inplace_stop_source source;
    auto token = source.get_token();
    EXPECT_TRUE(std::inplace_stop_source::stop_possible());
    EXPECT_TRUE(source.stop_possible());
    EXPECT_TRUE(token.stop_possible());
    EXPECT_FALSE(token.stop_requested());

    bool called = false;
    {
        std::inplace_stop_callback callback(token, [&] { called = true; });
    }
    EXPECT_FALSE(called);
}

TEST(ExecutionStopTokenTest, TokenFromConstSourceViewObservesStop) {
    std::inplace_stop_source source;
    const auto& view = source;
    bool called = false;
    std::inplace_stop_callback callback(view.get_token(), [&] { called = true; });

    EXPECT_TRUE(source.request_stop());
    EXPECT_TRUE(called);
    EXPECT_TRUE(view.get_token().stop_requested());
}

TEST(ExecutionStopTokenTest, CallbackInvocationPreservesCallbackTypeCategory) {
    for (bool already_stopped : {false, true}) {
        std::inplace_stop_source source;
        if (already_stopped) {
            source.request_stop();
        }
        int lvalue_calls = 0;
        int rvalue_calls = 0;
        int rvalue_only_calls = 0;
        qualified_stop_callback function{&lvalue_calls, &rvalue_calls};
        std::inplace_stop_callback owned(source.get_token(), function);
        std::inplace_stop_callback<qualified_stop_callback&> borrowed(
            source.get_token(), function);
        std::inplace_stop_callback rvalue_only(
            source.get_token(), rvalue_stop_callback{&rvalue_only_calls});

        EXPECT_EQ(source.request_stop(), !already_stopped);
        EXPECT_EQ(lvalue_calls, 1);
        EXPECT_EQ(rvalue_calls, 1);
        EXPECT_EQ(rvalue_only_calls, 1);
    }
}

// ── T-4: Expanded stop-token coverage ───────────────────────────────────

TEST(ExecutionStopTokenTest, PostStopCallbackImmediateInvocation) {
    std::inplace_stop_source src;
    src.request_stop();

    bool called = false;
    std::inplace_stop_callback cb(src.get_token(), [&] { called = true; });
    EXPECT_TRUE(called);
}

TEST(ExecutionStopTokenTest, PostStopCallbackCanDestroyItself) {
    std::inplace_stop_source src;
    ASSERT_TRUE(src.request_stop());

    using callback_t = std::inplace_stop_callback<std::function<void()>>;
    auto* callback = static_cast<callback_t*>(::operator new(sizeof(callback_t)));
    bool called = false;
    auto destroy_self = [&] {
        called = true;
        auto* current = std::exchange(callback, nullptr);
        current->~callback_t();
        ::operator delete(current);
    };

    ::new(static_cast<void*>(callback)) callback_t(
        src.get_token(), std::function<void()>{destroy_self});

    EXPECT_TRUE(called);
    EXPECT_EQ(callback, nullptr);
}

TEST(ExecutionStopTokenTest, CallbackAutoDeregisterOnDestruction) {
    std::inplace_stop_source src;
    auto token = src.get_token();

    bool called = false;
    {
        std::inplace_stop_callback cb(token, [&] { called = true; });
    } // cb destroyed — auto-deregistered
    src.request_stop();
    EXPECT_FALSE(called);
}

TEST(ExecutionStopTokenTest, CallbackDestructionWaitsForConcurrentInvocation) {
    std::inplace_stop_source src;
    auto token = src.get_token();

    std::mutex mtx;
    std::condition_variable cv;
    bool entered = false;
    bool release = false;
    std::atomic<bool> destructor_returned = false;

    auto fn = [&] {
        std::unique_lock lk{mtx};
        entered = true;
        cv.notify_all();
        cv.wait(lk, [&] { return release; });
    };
    using callback_t = std::inplace_stop_callback<decltype(fn)>;
    auto cb = std::make_unique<callback_t>(token, fn);

    std::thread requester{[&] {
        EXPECT_TRUE(src.request_stop());
    }};

    {
        std::unique_lock lk{mtx};
        cv.wait(lk, [&] { return entered; });
    }

    std::thread destroyer{[&] {
        cb.reset();
        destructor_returned.store(true, std::memory_order_release);
    }};

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_FALSE(destructor_returned.load(std::memory_order_acquire));

    {
        std::lock_guard lk{mtx};
        release = true;
    }
    cv.notify_all();

    destroyer.join();
    requester.join();
    EXPECT_TRUE(destructor_returned.load(std::memory_order_acquire));
}

TEST(ExecutionStopTokenTest, CallbackCanDestroyPendingCallbackDuringRequestStop) {
    std::inplace_stop_source src;
    auto token = src.get_token();

    using callback_t = std::inplace_stop_callback<std::function<void()>>;
    std::unique_ptr<callback_t> first;
    std::unique_ptr<callback_t> second;
    bool first_called = false;
    bool second_called = false;

    second = std::make_unique<callback_t>(
        token,
        std::function<void()>{[&] { second_called = true; }});
    first = std::make_unique<callback_t>(
        token,
        std::function<void()>{[&] {
            first_called = true;
            second.reset();
        }});

    EXPECT_TRUE(src.request_stop());
    EXPECT_TRUE(first_called);
    EXPECT_FALSE(second_called);
}

TEST(ExecutionStopTokenTest, NeverStopTokenBehavior) {
    std::never_stop_token token{};
    EXPECT_FALSE(token.stop_requested());
    EXPECT_FALSE(token.stop_possible());
    EXPECT_TRUE(token == std::never_stop_token{});
}

TEST(ExecutionStopTokenTest, MissingEnvStopQueryFallsBackToNeverStopToken) {
    auto token = std::get_stop_token(env_without_stop_query{});

    static_assert(std::is_same_v<decltype(token), std::never_stop_token>);
    EXPECT_FALSE(token.stop_requested());
    EXPECT_FALSE(token.stop_possible());
}

TEST(ExecutionStopTokenTest, MemberStopQueryTakesPriority) {
    std::inplace_stop_source source;
    member_stop_env env{source.get_token()};

    EXPECT_TRUE(source.request_stop());
    auto token = std::get_stop_token(env);

    static_assert(std::is_same_v<decltype(token), std::inplace_stop_token>);
    EXPECT_TRUE(token.stop_requested());
}

TEST(ExecutionStopTokenTest, ComposedEnvPreservesMemberQueryPriority) {
    std::inplace_stop_source source;
    auto env = std::execution::env{member_stop_env{source.get_token()}};

    EXPECT_TRUE(source.request_stop());
    auto token = std::get_stop_token(env);

    static_assert(std::is_same_v<decltype(token), std::inplace_stop_token>);
    EXPECT_TRUE(token.stop_requested());
}

TEST(ExecutionStopTokenTest, DefaultConstructedToken) {
    std::inplace_stop_token token{};
    EXPECT_FALSE(token.stop_requested());
    EXPECT_FALSE(token.stop_possible());
}

// ── T-8: stoppable_token concept probes ──────────────────────────────────

static_assert(std::stoppable_token<std::inplace_stop_token>,
              "inplace_stop_token must satisfy stoppable_token");
static_assert(std::stoppable_token<std::never_stop_token>,
              "never_stop_token must satisfy stoppable_token");

static_assert(std::unstoppable_token<std::never_stop_token>,
              "never_stop_token must satisfy unstoppable_token");
static_assert(!std::unstoppable_token<std::inplace_stop_token>,
              "inplace_stop_token must NOT satisfy unstoppable_token");

static_assert(std::stoppable_token<static_unstoppable_token>);
static_assert(std::stoppable_token<nonstatic_constexpr_false_token>);
static_assert(std::stoppable_token<nonstatic_constexpr_true_token>);
static_assert(std::stoppable_token<static_constexpr_true_token>);
static_assert(std::stoppable_token<nonstatic_runtime_token>);
static_assert(std::stoppable_token<static_runtime_token>);
static_assert(std::stoppable_token<stateful_stop_token>);

static_assert(std::unstoppable_token<static_unstoppable_token>);
// Check the owner-measured compiler policy without using the header's switch.
// A native concept is the library's own; only the backport's policy is pinned.
#if !defined(__cpp_lib_inplace_stop_token) || (__cpp_lib_inplace_stop_token < 202400L)
#if !defined(__NVCC__) && !defined(__CUDACC__) && !defined(__EDG__) && \
    !defined(__INTEL_COMPILER) && !defined(__NVCOMPILER) && \
    ((defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 16) || \
     (defined(__clang__) && defined(__apple_build_version__) && __clang_major__ >= 21) || \
     (defined(_MSC_VER) && !defined(__clang__) && _MSC_VER >= 1952))
static_assert(std::unstoppable_token<nonstatic_constexpr_false_token>);
#else
static_assert(!std::unstoppable_token<nonstatic_constexpr_false_token>);
#endif
#endif
static_assert(!std::unstoppable_token<nonstatic_constexpr_true_token>);
static_assert(!std::unstoppable_token<static_constexpr_true_token>);
static_assert(!std::unstoppable_token<nonstatic_runtime_token>);
static_assert(!std::unstoppable_token<static_runtime_token>);
// The default token has no source, but a token with a source can stop.
static_assert(!std::unstoppable_token<stateful_stop_token>);

static_assert(!std::stoppable_token<int>,
              "int must not satisfy stoppable_token");

// ── T-9: sync_wait accessible from std::this_thread ─────────────────────

TEST(ExecutionStopTokenTest, SyncWaitFromThisThread) {
    auto result = std::this_thread::sync_wait(std::execution::just(99));
    ASSERT_TRUE(static_cast<bool>(result));
    EXPECT_EQ(std::get<0>(*result), 99);
}
