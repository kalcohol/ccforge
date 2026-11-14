// Paired micro-benchmark for the inplace_stop_source backport. The same source
// is built against two backport snapshots with identical flags; see
// run_paired.sh and MEASUREMENT_PROTOCOL.txt in this directory.
#if defined(FORGE_EXECUTION_TEST_STOP_HOOKS) || defined(FORGE_EXECUTION_TEST_STRICT_STOP_LIFETIME)
#error Build the benchmark without the test-only stop-token macros.
#endif

#include <execution>

#if defined(__cpp_lib_inplace_stop_token)
#error This benchmark measures the inplace_stop_source backport; a native implementation is present.
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string_view>
#include <thread>
#include <vector>

// Compiler barriers keep every result observable without volatile accesses in
// the timed region.
#if defined(__GNUC__) || defined(__clang__)
template<class T> inline void observe(const T& value) {
    asm volatile("" : : "m"(value) : "memory");
}
template<class T> inline void input_barrier(T& value) {
    asm volatile("" : "+m"(value) : : "memory");
}
// Timed lambdas are always inlined so that inlining heuristics, which react to
// the changed source member functions, cannot decide the comparison.
#define FORGE_BENCH_INLINE __attribute__((always_inline))
#else
#error This measurement harness requires GCC or Clang compiler barriers.
#endif

namespace {

struct count_fn {
    unsigned* calls;
    void operator()() const noexcept { ++*calls; }
};
using count_cb = std::inplace_stop_callback<count_fn>;

// Cache-line slots keep object placement from favouring either build.
template<class T>
struct alignas(64) slot {
    std::optional<T> value;
};

// Resets its own slot while it runs: the requester takes the self-destroy path.
struct self_reset_fn {
    void* owner;
    void (*reset)(void*) noexcept;
    void operator()() const noexcept { reset(owner); }
};
using self_cb = std::inplace_stop_callback<self_reset_fn>;

constexpr std::size_t kBatch = 256;
constexpr std::size_t kMaxCallbacks = 16;
// Lifecycle kernels run iterations / (callbacks + 1) times; keep every kernel
// well above zero runs.
constexpr std::size_t kMinIterations = 1000;
constexpr std::array<std::size_t, 4> kCounts{0, 1, 4, 16};
constexpr std::array<std::string_view, 4> kRequestNames{
    "request_stop_0", "request_stop_1", "request_stop_4", "request_stop_16"};
constexpr std::array<std::string_view, 4> kLifecycleNames{
    "lifecycle_0", "lifecycle_1", "lifecycle_4", "lifecycle_16"};

void emit(std::string_view kernel, std::size_t iterations, double ns) {
    std::cout << "stop_bench,kernel=" << kernel << ",iterations=" << iterations
              << ",ns=" << std::setprecision(17) << ns << '\n';
}

template<class F>
void measure(std::string_view kernel, std::size_t iterations, F kernel_body) {
    for (int i = 0; i < 1024; ++i) kernel_body();
    const auto start = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < iterations; ++i) kernel_body();
    emit(kernel, iterations, std::chrono::duration<double, std::nano>(
        std::chrono::steady_clock::now() - start).count());
}

// Times only `timed`; setup and teardown of every batch stay outside the clock.
template<class Setup, class Timed, class Teardown>
void measure_batches(std::string_view kernel, std::size_t batches,
                     Setup setup, Timed timed, Teardown teardown) {
    setup();
    timed();
    teardown();
    double ns = 0;
    for (std::size_t batch = 0; batch < batches; ++batch) {
        setup();
        const auto start = std::chrono::steady_clock::now();
        timed();
        ns += std::chrono::duration<double, std::nano>(
            std::chrono::steady_clock::now() - start).count();
        teardown();
    }
    emit(kernel, batches * kBatch, ns);
}

void run_single(std::size_t iterations) {
    unsigned calls = 0;
    slot<std::inplace_stop_source> live;
    slot<std::inplace_stop_source> stopped;
    live.value.emplace();
    stopped.value.emplace();
    stopped.value->request_stop();
    auto* live_source = &*live.value;
    auto* stopped_source = &*stopped.value;

    // Controls: no changed code runs here.
    measure("barrier", iterations, [&]() FORGE_BENCH_INLINE {
        input_barrier(live_source);
        observe(live_source);
    });
    measure("stop_requested", iterations, [&]() FORGE_BENCH_INLINE {
        input_barrier(live_source);
        observe(live_source->stop_requested());
    });
    // Returns false from the early acquire check, before any lock operation.
    measure("losing_request_stop", iterations, [&]() FORGE_BENCH_INLINE {
        input_barrier(stopped_source);
        observe(stopped_source->request_stop());
    });
    {
        slot<count_cb> callback;
        measure("register_deregister", iterations, [&]() FORGE_BENCH_INLINE {
            input_barrier(live_source);
            callback.value.emplace(live_source->get_token(), count_fn{&calls});
            callback.value.reset();
            observe(callback);
        });
    }
    {
        std::array<slot<count_cb>, 4> resident;
        for (auto& entry : resident) entry.value.emplace(live_source->get_token(), count_fn{&calls});
        slot<count_cb> callback;
        measure("register_deregister_4live", iterations, [&]() FORGE_BENCH_INLINE {
            input_barrier(live_source);
            callback.value.emplace(live_source->get_token(), count_fn{&calls});
            callback.value.reset();
            observe(callback);
        });
        for (auto& entry : resident) entry.value.reset();
    }
    {
        slot<count_cb> callback;
        measure("post_stop_register", iterations, [&]() FORGE_BENCH_INLINE {
            input_barrier(stopped_source);
            callback.value.emplace(stopped_source->get_token(), count_fn{&calls});
            callback.value.reset();
            observe(calls);
        });
    }

    const std::size_t batches = std::max<std::size_t>(16, iterations / 4096);
    std::vector<slot<std::inplace_stop_source>> sources(kBatch);
    std::vector<slot<count_cb>> callbacks(kBatch * kMaxCallbacks);
    for (std::size_t index = 0; index < kCounts.size(); ++index) {
        const std::size_t count = kCounts[index];
        measure_batches(kRequestNames[index], batches,
            [&] {
                for (std::size_t i = 0; i < kBatch; ++i) {
                    sources[i].value.emplace();
                    for (std::size_t k = 0; k < count; ++k) {
                        callbacks[i * kMaxCallbacks + k].value.emplace(
                            sources[i].value->get_token(), count_fn{&calls});
                    }
                }
            },
            [&]() FORGE_BENCH_INLINE {
                for (std::size_t i = 0; i < kBatch; ++i) {
                    auto* source = &*sources[i].value;
                    input_barrier(source);
                    observe(source->request_stop());
                }
            },
            [&] {
                for (std::size_t i = 0; i < kBatch; ++i) {
                    for (std::size_t k = 0; k < count; ++k) {
                        callbacks[i * kMaxCallbacks + k].value.reset();
                    }
                    sources[i].value.reset();
                }
            });
    }
    {
        std::vector<slot<self_cb>> selves(kBatch);
        measure_batches("self_destroy", batches,
            [&] {
                for (std::size_t i = 0; i < kBatch; ++i) {
                    sources[i].value.emplace();
                    selves[i].value.emplace(sources[i].value->get_token(), self_reset_fn{
                        &selves[i], [](void* owner) noexcept {
                            static_cast<slot<self_cb>*>(owner)->value.reset();
                        }});
                }
            },
            [&]() FORGE_BENCH_INLINE {
                for (std::size_t i = 0; i < kBatch; ++i) {
                    auto* source = &*sources[i].value;
                    input_barrier(source);
                    observe(source->request_stop());
                }
            },
            [&] {
                for (std::size_t i = 0; i < kBatch; ++i) sources[i].value.reset();
            });
    }
    for (std::size_t index = 0; index < kCounts.size(); ++index) {
        const std::size_t count = kCounts[index];
        slot<std::inplace_stop_source> source;
        std::array<slot<count_cb>, kMaxCallbacks> local;
        measure(kLifecycleNames[index], iterations / (count + 1), [&]() FORGE_BENCH_INLINE {
            source.value.emplace();
            auto* current = &*source.value;
            input_barrier(current);
            for (std::size_t k = 0; k < count; ++k) {
                local[k].value.emplace(current->get_token(), count_fn{&calls});
            }
            observe(current->request_stop());
            for (std::size_t k = 0; k < count; ++k) local[k].value.reset();
            source.value.reset();
            observe(calls);
        });
    }
    observe(calls);
}

// Informational only: throughput of register/deregister on one shared source.
int run_contended(unsigned threads, unsigned milliseconds, unsigned work) {
    struct alignas(64) counter {
        unsigned long long value = 0;
    };
    slot<std::inplace_stop_source> shared;
    shared.value.emplace();
    std::vector<counter> operations(threads);
    std::atomic<unsigned> ready{0};
    std::atomic<bool> go{false};
    std::atomic<bool> finish{false};
    std::vector<std::thread> pool;
    pool.reserve(threads);
    for (unsigned t = 0; t < threads; ++t) {
        pool.emplace_back([&, t] {
            unsigned calls = 0;
            slot<count_cb> callback;
            unsigned long long count = 0;
            ready.fetch_add(1, std::memory_order_relaxed);
            while (!go.load(std::memory_order_acquire)) {}
            while (!finish.load(std::memory_order_relaxed)) {
                callback.value.emplace(shared.value->get_token(), count_fn{&calls});
                for (unsigned unit = 0; unit < work; ++unit) observe(unit);
                callback.value.reset();
                for (unsigned unit = 0; unit < work; ++unit) observe(unit);
                ++count;
            }
            operations[t].value = count;
            observe(calls);
        });
    }
    while (ready.load(std::memory_order_relaxed) != threads) std::this_thread::yield();
    const auto start = std::chrono::steady_clock::now();
    go.store(true, std::memory_order_release);
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    finish.store(true, std::memory_order_relaxed);
    for (auto& thread : pool) thread.join();
    const double ns = std::chrono::duration<double, std::nano>(
        std::chrono::steady_clock::now() - start).count();
    unsigned long long total = 0;
    unsigned long long least = operations[0].value;
    unsigned long long most = operations[0].value;
    for (const auto& entry : operations) {
        total += entry.value;
        least = std::min(least, entry.value);
        most = std::max(most, entry.value);
    }
    const double mean = static_cast<double>(total) / threads;
    std::cout << "stop_contended,threads=" << threads << ",work=" << work
              << ",ms=" << milliseconds << ",operations=" << total
              << ",ns=" << std::setprecision(17) << ns
              << ",min_share=" << (mean > 0 ? least / mean : 0.0)
              << ",max_share=" << (mean > 0 ? most / mean : 0.0) << '\n';
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc >= 2 && std::string_view(argv[1]) == "--contended") {
        if (argc != 5) return 2;
        const auto threads = std::strtoul(argv[2], nullptr, 10);
        const auto milliseconds = std::strtoul(argv[3], nullptr, 10);
        const auto work = std::strtoul(argv[4], nullptr, 10);
        if (threads == 0 || threads > 256 || milliseconds == 0 || work > 1000000) return 2;
        return run_contended(static_cast<unsigned>(threads), static_cast<unsigned>(milliseconds),
            static_cast<unsigned>(work));
    }
    if (argc > 2) return 2;
    const std::size_t iterations = argc == 2 ? std::strtoull(argv[1], nullptr, 10) : 1000000;
    if (iterations < kMinIterations) return 2;
    run_single(iterations);
    return 0;
}
