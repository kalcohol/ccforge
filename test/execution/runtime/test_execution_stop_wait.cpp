#include <gtest/gtest.h>
#include <execution>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <semaphore>
#include <thread>
#include <vector>
#if defined(__linux__)
#include <fstream>
#include <string>
#include <sys/syscall.h>
#include <unistd.h>
#endif

#if defined(FORGE_EXECUTION_TEST_STOP_HOOKS) && !defined(__cpp_lib_inplace_stop_token)

namespace {

using std::__forge_stop_detail::stop_site;
using namespace std::chrono_literals;
using fn_cb = std::inplace_stop_callback<std::function<void()>>;

#if defined(__SANITIZE_THREAD__) || defined(__SANITIZE_ADDRESS__)
constexpr bool kSanitized = true;
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer) || __has_feature(address_sanitizer)
constexpr bool kSanitized = true;
#else
constexpr bool kSanitized = false;
#endif
#else
constexpr bool kSanitized = false;
#endif

constexpr std::chrono::seconds kParkBudget = kSanitized ? 120s : 30s;
constexpr int kContenders = 4;

// Turns a lost wakeup or deadlock into a prompt failure instead of a hang
// until the CTest timeout.
class deadline_guard {
public:
    deadline_guard(const char* name, std::chrono::seconds limit) : name_(name) {
        thread_ = std::thread([this, limit] {
            std::unique_lock lock{mutex_};
            if (!cv_.wait_for(lock, limit, [this] { return done_; })) {
                std::fprintf(stderr, "DEADLINE: %s (suspected lost wakeup or deadlock)\n", name_);
                std::fflush(stderr);
                std::_Exit(124);
            }
        });
    }
    ~deadline_guard() {
        {
            std::lock_guard lock{mutex_};
            done_ = true;
        }
        cv_.notify_all();
        thread_.join();
    }
    deadline_guard(const deadline_guard&) = delete;
    deadline_guard& operator=(const deadline_guard&) = delete;

private:
    const char* name_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool done_ = false;
    std::thread thread_;
};

long current_tid() {
#if defined(__linux__)
    return static_cast<long>(::syscall(SYS_gettid));
#else
    return 0;
#endif
}

// Linux: true once the thread is blocked in futex, false after 10 s. Where the
// syscall file is unavailable, sleeps briefly and returns true; the scenario
// then checks progress only.
bool confirm_parked(long tid) {
#if defined(__linux__) && (defined(__x86_64__) || defined(__aarch64__))
#if defined(__x86_64__)
    constexpr int futex_number = 202;
#else
    constexpr int futex_number = 98;
#endif
    const auto path = "/proc/self/task/" + std::to_string(tid) + "/syscall";
    if (!std::ifstream(path)) {
        std::this_thread::sleep_for(50ms);
        return true;
    }
    const auto end = std::chrono::steady_clock::now() + 10s;
    while (std::chrono::steady_clock::now() < end) {
        std::ifstream in(path);
        std::string first;
        if ((in >> first) && first != "running" && std::atoi(first.c_str()) == futex_number) {
            return true;
        }
        std::this_thread::sleep_for(1ms);
    }
    return false;
#else
    (void)tid;
    std::this_thread::sleep_for(50ms);
    return true;
#endif
}

// Observes one thread (or threads sharing a role). It can hold the source
// lock once at a chosen unlock site and counts parks and notifications.
struct thread_probe {
    std::optional<stop_site> hold_at;
    std::atomic<bool> held{false};
    std::binary_semaphore reached{0};
    std::binary_semaphore go{0};
    std::atomic<int> parks{0};
    std::binary_semaphore parked{0};
    std::atomic<long> park_tid{0};
    std::atomic<int> lock_notifies{0};
    std::atomic<int> completions{0};

    static void hook(void* context, stop_site site) noexcept {
        auto* self = static_cast<thread_probe*>(context);
        switch (site) {
        case stop_site::lock_park:
            if (self->parks.fetch_add(1) == 0) {
                self->park_tid = current_tid();
                self->parked.release();
            }
            return;
        case stop_site::lock_notify:
            self->lock_notifies.fetch_add(1);
            return;
        case stop_site::completion_published:
            self->completions.fetch_add(1);
            break;
        default:
            break;
        }
        if (self->hold_at && *self->hold_at == site && !self->held.exchange(true)) {
            self->reached.release();
            self->go.acquire();
        }
    }
};

// Installs an observer for the calling thread and clears every thread-local
// hook on scope exit, so later tests on this thread see no stale pointer.
class probe_scope {
public:
    probe_scope() noexcept = default;
    explicit probe_scope(thread_probe& probe) noexcept {
        std::__forge_stop_detail::event_context = &probe;
        std::__forge_stop_detail::event_hook = &thread_probe::hook;
    }
    ~probe_scope() {
        std::__forge_stop_detail::event_hook = nullptr;
        std::__forge_stop_detail::event_context = nullptr;
        std::__forge_stop_detail::deregister_wait_hook = nullptr;
        std::__forge_stop_detail::deregister_context = nullptr;
    }
    probe_scope(const probe_scope&) = delete;
    probe_scope& operator=(const probe_scope&) = delete;
};

void release_semaphore(void* context) noexcept {
    static_cast<std::binary_semaphore*>(context)->release();
}

const char* site_name(stop_site site) {
    static const char* const names[] = {"add", "add_stopped", "request_lost", "request_invoke",
        "request_final", "remove_pending", "remove_completed", "remove_self", "remove_await",
        "remove_handoff"};
    return names[static_cast<int>(site)];
}

struct contender_result {
    std::atomic<int> calls{0};
    std::atomic<bool> on_own_thread{true};
    std::atomic<bool> done{false};
};

// A holder blocks at `site` with the source lock held, kContenders threads
// park in lock(), and one release must let every contender through.
void contend_at(stop_site site) {
    SCOPED_TRACE(site_name(site));
    deadline_guard guard{site_name(site), kParkBudget};
    std::inplace_stop_source source;
    thread_probe holder;
    holder.hold_at = site;
    thread_probe side;
    std::vector<std::thread> aux;
    std::thread holder_thread;
    std::binary_semaphore entered{0}, release{0}, committed{0};
    std::unique_ptr<fn_cb> cb;
    bool release_after = false;

    switch (site) {
    case stop_site::add:
        holder_thread = std::thread([&] {
            probe_scope scope{holder};
            fn_cb c(source.get_token(), [] {});
        });
        break;
    case stop_site::add_stopped:
        EXPECT_TRUE(source.request_stop());
        holder_thread = std::thread([&] {
            probe_scope scope{holder};
            fn_cb c(source.get_token(), [] {});
        });
        break;
    case stop_site::request_final:
        holder_thread = std::thread([&] {
            probe_scope scope{holder};
            EXPECT_TRUE(source.request_stop());
        });
        break;
    case stop_site::request_invoke:
        cb = std::make_unique<fn_cb>(source.get_token(), [] {});
        holder_thread = std::thread([&] {
            probe_scope scope{holder};
            EXPECT_TRUE(source.request_stop());
        });
        break;
    case stop_site::request_lost:
        side.hold_at = stop_site::add;
        aux.emplace_back([&] {
            probe_scope scope{side};
            fn_cb c(source.get_token(), [] {});
        });
        side.reached.acquire();
        // Both requesters pass the unlocked stop check and park; the loser holds.
        for (int i = 0; i != 2; ++i) {
            aux.emplace_back([&] {
                probe_scope scope{holder};
                (void)source.request_stop();
            });
        }
        while (holder.parks.load() < 2) std::this_thread::sleep_for(1ms);
        side.go.release();
        break;
    case stop_site::remove_pending:
        cb = std::make_unique<fn_cb>(source.get_token(), [] {});
        holder_thread = std::thread([&] {
            probe_scope scope{holder};
            cb.reset();
        });
        break;
    case stop_site::remove_completed:
        cb = std::make_unique<fn_cb>(source.get_token(), [] {});
        EXPECT_TRUE(source.request_stop());
        holder_thread = std::thread([&] {
            probe_scope scope{holder};
            cb.reset();
        });
        break;
    case stop_site::remove_self:
        cb = std::make_unique<fn_cb>(source.get_token(), [&] {
            auto* owner = &cb;
            owner->reset();
        });
        holder_thread = std::thread([&] {
            probe_scope scope{holder};
            EXPECT_TRUE(source.request_stop());
        });
        break;
    case stop_site::remove_await:
        cb = std::make_unique<fn_cb>(source.get_token(), [&] {
            entered.release();
            release.acquire();
        });
        aux.emplace_back([&] { EXPECT_TRUE(source.request_stop()); });
        entered.acquire();
        holder_thread = std::thread([&] {
            probe_scope scope{holder};
            cb.reset();
        });
        release_after = true;
        break;
    case stop_site::remove_handoff:
        cb = std::make_unique<fn_cb>(source.get_token(), [&] {
            entered.release();
            release.acquire();
        });
        aux.emplace_back([&] { EXPECT_TRUE(source.request_stop()); });
        entered.acquire();
        holder_thread = std::thread([&] {
            probe_scope scope{holder};
            std::__forge_stop_detail::deregister_context = &committed;
            std::__forge_stop_detail::deregister_wait_hook = &release_semaphore;
            cb.reset();
        });
        committed.acquire();
        release.release();
        break;
    default:
        FAIL() << "not an unlock site";
    }

    holder.reached.acquire();
    const bool stopped = site != stop_site::add && site != stop_site::remove_pending;
    std::vector<thread_probe> probes(kContenders);
    std::vector<contender_result> results(kContenders);
    std::vector<std::thread> threads;
    for (int k = 0; k != kContenders; ++k) {
        threads.emplace_back([&, k] {
            probe_scope scope{probes[k]};
            const auto self = std::this_thread::get_id();
            {
                fn_cb c(source.get_token(), [&results, k, self] {
                    results[k].calls.fetch_add(1);
                    if (std::this_thread::get_id() != self) results[k].on_own_thread = false;
                });
            }
            results[k].done = true;
        });
    }
    for (auto& probe : probes) probe.parked.acquire();
    for (auto& probe : probes) EXPECT_TRUE(confirm_parked(probe.park_tid.load()));
    for (auto& result : results) EXPECT_FALSE(result.done.load());
    holder.go.release();
    if (release_after) release.release();
    for (auto& thread : threads) thread.join();
    if (holder_thread.joinable()) holder_thread.join();
    for (auto& thread : aux) thread.join();
    for (auto& result : results) {
        EXPECT_TRUE(result.done.load());
        EXPECT_EQ(result.calls.load(), stopped ? 1 : 0);
        EXPECT_TRUE(result.on_own_thread.load());
    }
    EXPECT_GE(holder.lock_notifies.load(), 1);
}

constexpr std::chrono::seconds kStressBudget = kSanitized ? 300s : 120s;

// Two-party phase barrier built from header atomics (visible to TSan; libc++'s
// std::barrier algorithm lives in the uninstrumented dylib).
class pair_barrier {
public:
    void arrive_and_wait() {
        const int gen = generation_.load(std::memory_order_acquire);
        if (arrived_.fetch_add(1, std::memory_order_acq_rel) == 1) {
            arrived_.store(0, std::memory_order_relaxed);
            generation_.fetch_add(1, std::memory_order_acq_rel);
            generation_.notify_all();
        } else {
            while (generation_.load(std::memory_order_acquire) == gen) {
                generation_.wait(gen, std::memory_order_acquire);
            }
        }
    }

private:
    std::atomic<int> arrived_{0};
    std::atomic<int> generation_{0};
};


} // namespace

TEST(StopWaitTest, ParkedContendersWakeAtAdd) { contend_at(stop_site::add); }
TEST(StopWaitTest, ParkedContendersWakeAtAddStopped) { contend_at(stop_site::add_stopped); }
TEST(StopWaitTest, ParkedContendersWakeAtRequestLost) { contend_at(stop_site::request_lost); }
TEST(StopWaitTest, ParkedContendersWakeAtRequestInvoke) { contend_at(stop_site::request_invoke); }
TEST(StopWaitTest, ParkedContendersWakeAtRequestFinal) { contend_at(stop_site::request_final); }
TEST(StopWaitTest, ParkedContendersWakeAtRemovePending) { contend_at(stop_site::remove_pending); }
TEST(StopWaitTest, ParkedContendersWakeAtRemoveCompleted) { contend_at(stop_site::remove_completed); }
TEST(StopWaitTest, ParkedContendersWakeAtRemoveSelf) { contend_at(stop_site::remove_self); }
TEST(StopWaitTest, ParkedContendersWakeAtRemoveAwait) { contend_at(stop_site::remove_await); }
TEST(StopWaitTest, ParkedContendersWakeAtRemoveHandoff) { contend_at(stop_site::remove_handoff); }

TEST(StopWaitTest, UncontendedPathsIssueNoNotification) {
    deadline_guard guard{"UncontendedPathsIssueNoNotification", kParkBudget};
    thread_probe probe;
    probe_scope scope{probe};
    int waits = 0;
    std::__forge_stop_detail::deregister_context = &waits;
    std::__forge_stop_detail::deregister_wait_hook = [](void* context) noexcept {
        ++*static_cast<int*>(context);
    };
    {
        std::inplace_stop_source source;
        int calls = 0;
        auto completed = std::make_unique<fn_cb>(source.get_token(), [&] { ++calls; });
        fn_cb kept(source.get_token(), [&] { ++calls; });
        auto pending = std::make_unique<fn_cb>(source.get_token(), [&] { ++calls; });
        pending.reset();
        std::unique_ptr<fn_cb> self;
        self = std::make_unique<fn_cb>(source.get_token(), [&] {
            auto* owner = &self;
            owner->reset();
        });
        EXPECT_TRUE(source.request_stop());
        EXPECT_FALSE(source.request_stop());
        EXPECT_EQ(calls, 2);
        completed.reset();
        fn_cb late(source.get_token(), [&] { ++calls; });
        EXPECT_EQ(calls, 3);
    }
    EXPECT_EQ(probe.lock_notifies.load(), 0);
    EXPECT_EQ(probe.completions.load(), 0);
    EXPECT_EQ(probe.parks.load(), 0);
    EXPECT_EQ(waits, 0);
}

namespace {
struct flag_function {
    std::atomic<int>* calls;
    void operator()() const noexcept { calls->fetch_add(1, std::memory_order_relaxed); }
};

// Holds the source lock across a yield or a short spin at some registration
// and deregistration unlocks, so that contenders really park; counts parks.
struct contention_probe {
    std::atomic<long>* parks;
    unsigned seed;

    static void hook(void* context, stop_site site) noexcept {
        auto* self = static_cast<contention_probe*>(context);
        if (site == stop_site::lock_park) {
            self->parks->fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if (site != stop_site::add && site != stop_site::remove_pending) return;
        self->seed = self->seed * 1103515245u + 12345u;
        const unsigned roll = (self->seed >> 16) % 16;
        if (roll == 0) {
            std::this_thread::yield();
        } else if (roll < 4) {
            volatile unsigned sink = 0;
            for (unsigned i = 0; i != 200; ++i) sink = sink + 1;
        }
    }
};
} // namespace

TEST(StopWaitTest, LockContentionStress) {
    deadline_guard guard{"LockContentionStress", kStressBudget};
    constexpr int kThreads = 6;
    const int iterations = kSanitized ? 2000 : 30000;
    std::inplace_stop_source source;
    int witness_calls = 0;
    std::inplace_stop_callback witness(source.get_token(), [&] { ++witness_calls; });
    std::atomic<long> parks{0};
    std::atomic<int> calls{0};
    std::vector<std::thread> threads;
    for (int t = 0; t != kThreads; ++t) {
        threads.emplace_back([&, t] {
            contention_probe probe{&parks, 2654435761u * static_cast<unsigned>(t + 1)};
            probe_scope scope;
            std::__forge_stop_detail::event_context = &probe;
            std::__forge_stop_detail::event_hook = &contention_probe::hook;
            for (int i = 0; i != iterations; ++i) {
                std::inplace_stop_callback<flag_function> callback(source.get_token(), flag_function{&calls});
            }
        });
    }
    for (auto& thread : threads) thread.join();
    EXPECT_EQ(calls.load(), 0);
    EXPECT_TRUE(source.request_stop());
    EXPECT_EQ(witness_calls, 1);
    std::fprintf(stderr, "  LockContentionStress: %ld parks\n", parks.load());
    if (std::thread::hardware_concurrency() >= 2) {
        EXPECT_GT(parks.load(), 0);
    }
}

TEST(StopWaitTest, LockRelayRearmsAfterWakeup) {
    deadline_guard guard{"LockRelayRearmsAfterWakeup", kParkBudget};
    const int contenders = kContenders;
    std::inplace_stop_source source;
    thread_probe holder;
    holder.hold_at = stop_site::add;
    std::thread holder_thread([&] {
        probe_scope scope{holder};
        fn_cb c(source.get_token(), [] {});
    });
    holder.reached.acquire();
    std::vector<thread_probe> probes(contenders);
    std::vector<std::atomic<bool>> done(contenders);
    std::vector<std::thread> threads;
    for (int k = 0; k != contenders; ++k) {
        probes[k].hold_at = stop_site::add;
        threads.emplace_back([&, k] {
            probe_scope scope{probes[k]};
            { fn_cb c(source.get_token(), [] {}); }
            done[k] = true;
        });
    }
    for (auto& p : probes) p.parked.acquire();
    for (auto& p : probes) EXPECT_TRUE(confirm_parked(p.park_tid.load()));
    holder.go.release();
    std::vector<bool> released(contenders, false);
    for (int round = 0; round != contenders; ++round) {
        // Exactly one unreleased contender acquires the lock and holds it.
        int current = -1;
        while (current < 0) {
            for (int k = 0; k != contenders; ++k) {
                if (!released[k] && probes[k].held.load()) current = k;
            }
            if (current < 0) std::this_thread::sleep_for(100us);
        }
        probes[current].reached.acquire();
        // Every other unreleased contender was woken and must block again,
        // either through lock() (re-arming the bit) or inside the library wait
        // loop on a value whose bit another contender armed in this segment.
        std::this_thread::sleep_for(5ms);
        for (int k = 0; k != contenders; ++k) {
            if (k == current || released[k]) continue;
            EXPECT_TRUE(confirm_parked(probes[k].park_tid.load()));
        }
        released[current] = true;
        probes[current].go.release();
    }
    for (auto& t : threads) t.join();
    holder_thread.join();
    for (auto& d : done) EXPECT_TRUE(d.load());
}

TEST(StopWaitTest, CompletionWaiterParksAndIsWoken) {
    deadline_guard guard{"CompletionWaiterParksAndIsWoken", kParkBudget};
    std::inplace_stop_source source;
    std::binary_semaphore entered{0}, release{0}, committed{0};
    bool finished = false; // plain: destruction must happen after this write
    auto cb = std::make_unique<fn_cb>(source.get_token(), [&] {
        entered.release();
        release.acquire();
        finished = true;
    });
    thread_probe requester_probe;
    std::thread requester([&] {
        probe_scope scope{requester_probe};
        EXPECT_TRUE(source.request_stop());
    });
    entered.acquire();
    std::atomic<long> tid{0};
    std::atomic<bool> returned{false};
    bool seen = false;
    thread_probe probe;
    std::thread destroyer([&] {
        probe_scope scope{probe};
        tid = current_tid();
        std::__forge_stop_detail::deregister_context = &committed;
        std::__forge_stop_detail::deregister_wait_hook = &release_semaphore;
        cb.reset();
        seen = finished;
        returned = true;
    });
    committed.acquire();
    EXPECT_TRUE(confirm_parked(tid.load()));
    EXPECT_TRUE(!returned.load());
    release.release();
    destroyer.join();
    requester.join();
    EXPECT_TRUE(seen);
    EXPECT_EQ(requester_probe.completions.load(), 1);
}

TEST(StopWaitTest, DestroyerAfterCompletionNeitherWaitsNorIsNotified) {
    deadline_guard guard{"DestroyerAfterCompletionNeitherWaitsNorIsNotified", kParkBudget};
    std::inplace_stop_source source;
    std::binary_semaphore entered{0}, release{0};
    int quick_calls = 0;
    fn_cb blocker(source.get_token(), [&] {
        entered.release();
        release.acquire();
    });
    auto quick = std::make_unique<fn_cb>(source.get_token(), [&] { ++quick_calls; });
    thread_probe requester_probe;
    std::thread requester([&] {
        probe_scope scope{requester_probe};
        EXPECT_TRUE(source.request_stop());
    });
    entered.acquire(); // quick (newest, LIFO) already completed
    probe_scope scope;
    int hook_calls = 0;
    std::__forge_stop_detail::deregister_context = &hook_calls;
    std::__forge_stop_detail::deregister_wait_hook = [](void* c) noexcept { ++*static_cast<int*>(c); };
    quick.reset();
    std::__forge_stop_detail::deregister_wait_hook = nullptr;
    EXPECT_TRUE(hook_calls == 0);
    EXPECT_TRUE(quick_calls == 1);
    release.release();
    requester.join();
    EXPECT_TRUE(requester_probe.completions.load() == 0);
}

TEST(StopWaitTest, FinalHandoffParksUntilRequesterUnlocks) {
    deadline_guard guard{"FinalHandoffParksUntilRequesterUnlocks", kParkBudget};
    std::inplace_stop_source source;
    std::binary_semaphore entered{0}, release{0}, committed{0};
    auto cb = std::make_unique<fn_cb>(source.get_token(), [&] {
        entered.release();
        release.acquire();
    });
    thread_probe requester_probe;
    requester_probe.hold_at = stop_site::request_final;
    std::thread requester([&] {
        probe_scope scope{requester_probe};
        EXPECT_TRUE(source.request_stop());
    });
    entered.acquire();
    thread_probe destroyer_probe;
    std::atomic<bool> returned{false};
    std::thread destroyer([&] {
        probe_scope scope{destroyer_probe};
        std::__forge_stop_detail::deregister_context = &committed;
        std::__forge_stop_detail::deregister_wait_hook = &release_semaphore;
        cb.reset();
        returned = true;
    });
    committed.acquire();
    release.release();
    requester_probe.reached.acquire(); // stored, notified, still locked
    // The handoff lock() must park; the destroyer must not return meanwhile.
    const bool parked = destroyer_probe.parked.try_acquire_for(5s);
    if (parked) { EXPECT_TRUE(confirm_parked(destroyer_probe.park_tid.load())); }
    const bool early = returned.load();
    EXPECT_TRUE(requester_probe.completions.load() == 1);
    requester_probe.go.release();
    destroyer.join();
    requester.join();
    EXPECT_TRUE(parked && !early);
    EXPECT_TRUE(returned.load());
    EXPECT_TRUE(requester_probe.lock_notifies.load() >= 1);
}

TEST(StopWaitTest, HandoffCoversNotifyAfterEarlyCompletionObservation) {
    deadline_guard guard{"HandoffCoversNotifyAfterEarlyCompletionObservation", kParkBudget};
    std::inplace_stop_source source;
    std::binary_semaphore entered{0}, release{0}, committed{0}, proceed{0};
    auto cb = std::make_unique<fn_cb>(source.get_token(), [&] {
        entered.release();
        release.acquire();
    });
    thread_probe requester_probe;
    requester_probe.hold_at = stop_site::completion_published;
    std::thread requester([&] {
        probe_scope scope{requester_probe};
        EXPECT_TRUE(source.request_stop());
    });
    entered.acquire();
    struct gate {
        std::binary_semaphore* committed;
        std::binary_semaphore* proceed;
    } g{&committed, &proceed};
    thread_probe destroyer_probe;
    std::atomic<bool> returned{false};
    std::thread destroyer([&] {
        probe_scope scope{destroyer_probe};
        std::__forge_stop_detail::deregister_context = &g;
        std::__forge_stop_detail::deregister_wait_hook = [](void* c) noexcept {
            auto* gg = static_cast<gate*>(c);
            gg->committed->release();
            gg->proceed->acquire();
        };
        cb.reset();
        returned = true;
    });
    committed.acquire();
    release.release();
    requester_probe.reached.acquire(); // completion == 1 stored, notify not yet issued
    proceed.release();                 // destroyer's first load observes 1
    // Its handoff lock() must park until the requester's notify has returned.
    // The requester is released either way, so under ASan + strict mode a
    // missing handoff surfaces as heap-use-after-free on the post-notify probe.
    const bool parked = destroyer_probe.parked.try_acquire_for(5s);
    if (parked) { EXPECT_TRUE(confirm_parked(destroyer_probe.park_tid.load())); }
    const bool early = returned.load();
    requester_probe.go.release();
    destroyer.join();
    requester.join();
    EXPECT_TRUE(parked && !early);
    EXPECT_TRUE(returned.load());
}

TEST(StopWaitTest, CompletionOrderRaceStress) {
    deadline_guard guard{"CompletionOrderRaceStress", kStressBudget};
    const int iterations = kSanitized ? 3000 : 20000;
    std::atomic<int> awaited{0};
    pair_barrier sync;
    std::unique_ptr<std::inplace_stop_source> source;
    std::unique_ptr<fn_cb> cb;
    std::atomic<bool> entered{false};
    bool finished = false;
    std::thread destroyer([&] {
        probe_scope scope;
        std::minstd_rand rng{7};
        std::__forge_stop_detail::deregister_context = &awaited;
        std::__forge_stop_detail::deregister_wait_hook = [](void* c) noexcept {
            static_cast<std::atomic<int>*>(c)->fetch_add(1);
        };
        for (int i = 0; i != iterations; ++i) {
            sync.arrive_and_wait();
            while (!entered.load(std::memory_order_acquire)) {}
            volatile unsigned sink = 0;
            for (unsigned spin = rng() % 1024; spin != 0; --spin) sink = sink + 1;
            cb.reset();
            EXPECT_TRUE(finished);
            sync.arrive_and_wait();
        }
    });
    std::minstd_rand rng{11};
    for (int i = 0; i != iterations; ++i) {
        source = std::make_unique<std::inplace_stop_source>();
        entered.store(false, std::memory_order_relaxed);
        finished = false;
        const unsigned work = rng() % 4096;
        cb = std::make_unique<fn_cb>(source->get_token(), [&, work] {
            entered.store(true, std::memory_order_release);
            volatile unsigned sink = 0;
            for (unsigned spin = work; spin != 0; --spin) sink = sink + 1;
            finished = true;
        });
        sync.arrive_and_wait();
        EXPECT_TRUE(source->request_stop());
        sync.arrive_and_wait();
        source.reset();
    }
    destroyer.join();
    const int waited = awaited.load();
    std::fprintf(stderr, "  completion_race_stress: %d/%d awaited, %d completed-first\n", waited, iterations,
        iterations - waited);
    // The split depends on scheduling (a single CPU may produce only one
    // ordering), so it is informational. CompletionWaiterParksAndIsWoken and
    // DestroyerAfterCompletionNeitherWaitsNorIsNotified force each ordering.
}

TEST(StopWaitTest, OversubscribedRegisterRequestDestroyStress) {
    deadline_guard guard{"OversubscribedRegisterRequestDestroyStress", kStressBudget};
    const int rounds = kSanitized ? 100 : 500;
    constexpr int registrations = 12;
    const int workers = 16;
    std::mutex mutex;
    std::condition_variable cv;
    int generation = 0;
    int finished = 0;
    std::inplace_stop_source* current = nullptr;
    std::atomic<int> winners{0};
    std::atomic<bool> duplicate{false};
    std::vector<std::thread> threads;
    for (int worker = 0; worker != workers; ++worker) {
        threads.emplace_back([&, worker, registrations] {
            for (int round = 0; round != rounds; ++round) {
                {
                    std::unique_lock lock{mutex};
                    cv.wait(lock, [&] { return generation > round; });
                }
                if (worker < 2 || worker >= 4) {
                    for (int index = 0; index != registrations; ++index) {
                        std::atomic<unsigned> calls{0};
                        {
                            std::inplace_stop_callback callback(current->get_token(), [&] {
                                if (calls.fetch_add(1, std::memory_order_relaxed) != 0)
                                    duplicate.store(true, std::memory_order_relaxed);
                            });
                            if ((index + round + worker) % 3 == 0) std::this_thread::yield();
                        }
                    }
                } else if (current->request_stop()) {
                    winners.fetch_add(1, std::memory_order_relaxed);
                }
                {
                    std::lock_guard lock{mutex};
                    ++finished;
                }
                cv.notify_all();
            }
        });
    }
    for (int round = 0; round != rounds; ++round) {
        std::inplace_stop_source source;
        current = &source;
        int witness_calls = 0;
        std::inplace_stop_callback witness(source.get_token(), [&] { ++witness_calls; });
        winners.store(0, std::memory_order_relaxed);
        {
            std::lock_guard lock{mutex};
            finished = 0;
            generation = round + 1;
        }
        cv.notify_all();
        {
            std::unique_lock lock{mutex};
            cv.wait(lock, [&] { return finished == workers; });
        }
        EXPECT_TRUE(winners.load(std::memory_order_relaxed) == 1);
        EXPECT_TRUE(witness_calls == 1);
        EXPECT_TRUE(source.stop_requested());
    }
    for (auto& t : threads) t.join();
    EXPECT_TRUE(!duplicate.load());
}

#if defined(FORGE_EXECUTION_TEST_STRICT_STOP_LIFETIME)
namespace {
struct noop_function {
    void operator()() const noexcept {}
};

// Death-test body: hold the lock at try_add_callback, then destroy the source.
[[noreturn]] void destroy_source_while_locked(bool with_parked_contender) {
    std::thread watchdog([] {
        std::this_thread::sleep_for(30s);
        std::fputs("DEADLINE: strict destruction scenario\n", stderr);
        std::_Exit(124);
    });
    watchdog.detach();
    std::optional<std::inplace_stop_source> source{std::in_place};
    thread_probe holder;
    holder.hold_at = stop_site::add;
    std::thread holding([&] {
        probe_scope scope{holder};
        std::inplace_stop_callback<noop_function> callback(source->get_token(), noop_function{});
    });
    holder.reached.acquire();
    if (with_parked_contender) {
        thread_probe contender;
        std::thread waiting([&] {
            probe_scope scope{contender};
            std::inplace_stop_callback<noop_function> callback(source->get_token(), noop_function{});
        });
        contender.parked.acquire();
        (void)confirm_parked(contender.park_tid.load());
        source.reset(); // the strict destructor must wake the parked contender
        waiting.join();
    } else {
        source.reset(); // the holder's unlock must detect the tombstone
        holder.go.release();
        holding.join();
    }
    std::_Exit(0);
}
} // namespace

TEST(StopWaitDeathTest, DetectsSourceDestructionWhileLockHeld) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    EXPECT_DEATH(destroy_source_while_locked(false), "inplace stop source accessed after destruction");
}

TEST(StopWaitDeathTest, StrictDestructorWakesParkedContender) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    EXPECT_DEATH(destroy_source_while_locked(true), "inplace stop source accessed after destruction");
}
#endif

#else
TEST(StopWaitTest, RequiresBackportStopHooks) {
    GTEST_SKIP() << "needs the inplace_stop_source backport with FORGE_EXECUTION_TEST_STOP_HOOKS";
}
#endif
