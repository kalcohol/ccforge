#include <hive>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <exception>
#include <list>
#include <memory>
#include <random>

namespace {
constexpr std::size_t population = 1000000;
constexpr std::size_t sample_count = 31;
using clock_type = std::chrono::steady_clock;

struct statistics {
    std::size_t live = 0;
    std::size_t peak = 0;
    std::size_t calls = 0;
} stats;
volatile long long observed_sum = 0;

template<class T>
struct measured_allocator {
    using value_type = T;
    measured_allocator() = default;
    template<class U> measured_allocator(const measured_allocator<U>&) noexcept {}
    T* allocate(std::size_t n) {
        T* p = std::allocator<T>{}.allocate(n);
        ++stats.calls;
        stats.live += n * sizeof(T);
        stats.peak = std::max(stats.peak, stats.live);
        return p;
    }
    void deallocate(T* p, std::size_t n) noexcept {
        stats.live -= n * sizeof(T);
        std::allocator<T>{}.deallocate(p, n);
    }
    template<class U> bool operator==(const measured_allocator<U>&) const noexcept { return true; }
};

struct ordinary64 {
    int value;
    unsigned char padding[60]{};
    explicit ordinary64(int v) : value(v) {}
    explicit operator int() const noexcept { return value; }
};
struct alignas(64) aligned64 {
    int value;
    unsigned char padding[60]{};
    explicit aligned64(int v) : value(v) {}
    explicit operator int() const noexcept { return value; }
};
static_assert(sizeof(ordinary64) == 64 && sizeof(aligned64) == 64);

template<class C>
long long sum_values(const C& container) {
    long long sum = 0;
    for (const auto& value : container) sum += static_cast<int>(value);
    observed_sum = sum;
    return sum;
}

struct traversal_result {
    double minimum_ns;
    double median_ns;
    long long checksum;
};

template<class C>
traversal_result measure_traversal(const C& container) {
    sum_values(container);
    std::array<double, sample_count> samples{};
    long long checksum = 0;
    for (double& sample : samples) {
        const auto begin = clock_type::now();
        checksum = sum_values(container);
        const auto end = clock_type::now();
        sample = std::chrono::duration<double, std::nano>(end - begin).count();
    }
    std::sort(samples.begin(), samples.end());
    return {samples.front(), samples[sample_count / 2], checksum};
}

template<class C>
void add(C& container, int value) {
    if constexpr (requires { container.push_back(typename C::value_type(value)); })
        container.push_back(typename C::value_type(value));
    else
        container.insert(typename C::value_type(value));
}

template<class C>
bool run(C& container, const char* kind, const char* type, const char* block_max) {
    const auto w1_begin = clock_type::now();
    for (std::size_t i = 0; i < population; ++i) add(container, static_cast<int>(i));
    const auto w1_end = clock_type::now();
    const statistics dense_stats = stats;
    const std::size_t dense_size = container.size();
    std::size_t dense_capacity = dense_size;
    if constexpr (requires { container.capacity(); }) dense_capacity = container.capacity();
    const auto dense = measure_traversal(container);
    constexpr long long expected = static_cast<long long>(population) * (population - 1) / 2;
    if (dense.checksum != expected) {
        std::fprintf(stderr, "dense checksum mismatch: %lld, expected %lld\n", dense.checksum, expected);
        return false;
    }

    std::mt19937 rng(1);
    const auto w2_begin = clock_type::now();
    for (int round = 0; round < 3; ++round) {
        std::size_t removed = 0;
        for (auto it = container.begin(); it != container.end();) {
            if (rng() & 1u) {
                it = container.erase(it);
                ++removed;
            } else {
                ++it;
            }
        }
        for (std::size_t i = 0; i < removed; ++i)
            add(container, static_cast<int>(rng() & 0xffffu));
    }
    const auto w2_end = clock_type::now();
    if (container.size() != population) {
        std::fprintf(stderr, "churn size mismatch: %zu\n", container.size());
        return false;
    }
    const auto churn = measure_traversal(container);
    const double w1_ns = std::chrono::duration<double, std::nano>(w1_end - w1_begin).count();
    const double w2_ns = std::chrono::duration<double, std::nano>(w2_end - w2_begin).count();
    std::puts("container,type,block_max,size_w1,capacity_w1,live_bytes_w1,peak_bytes_w1,allocation_calls_w1,bytes_per_element_w1,w1_ns,w2_ns,dense_min_ns,dense_median_ns,churn_min_ns,churn_median_ns,dense_checksum,churn_checksum");
    std::printf("%s,%s,%s,%zu,%zu,%zu,%zu,%zu,%.6f,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%lld,%lld\n",
        kind, type, block_max, dense_size, dense_capacity, dense_stats.live,
        dense_stats.peak, dense_stats.calls, double(dense_stats.live) / dense_size,
        w1_ns, w2_ns, dense.minimum_ns, dense.median_ns, churn.minimum_ns,
        churn.median_ns, dense.checksum, churn.checksum);
    return true;
}

template<class T>
bool execute(const char* kind, const char* type, const char* block_max, std::size_t maximum) {
    bool ok;
    {
        if (std::strcmp(kind, "list") == 0) {
            std::list<T, measured_allocator<T>> container;
            ok = run(container, kind, type, block_max);
        } else {
            using hive_type = std::hive<T, measured_allocator<T>>;
            if (maximum == 0) {
                hive_type container;
                ok = run(container, kind, type, block_max);
            } else {
                const auto minimum = hive_type::block_capacity_default_limits().min;
                hive_type container(std::hive_limits{minimum, maximum});
                ok = run(container, kind, type, block_max);
            }
        }
    }
    if (stats.live != 0) {
        std::fprintf(stderr, "allocator reports %zu bytes still live\n", stats.live);
        return false;
    }
    return ok;
}
}

int main(int argc, char** argv) {
    if (argc < 3 || argc > 4 ||
        (std::strcmp(argv[1], "list") != 0 && std::strcmp(argv[1], "hive") != 0)) {
        std::fprintf(stderr, "usage: %s {list|hive} {int|double|ordinary64|align64} [default|256|1024|4096|8192]\n", argv[0]);
        return 2;
    }
    const char* maximum_text = argc == 4 ? argv[3] : "default";
    std::size_t maximum = 0;
    if (std::strcmp(maximum_text, "256") == 0) maximum = 256;
    else if (std::strcmp(maximum_text, "1024") == 0) maximum = 1024;
    else if (std::strcmp(maximum_text, "4096") == 0) maximum = 4096;
    else if (std::strcmp(maximum_text, "8192") == 0) maximum = 8192;
    else if (std::strcmp(maximum_text, "default") != 0) {
        std::fprintf(stderr, "invalid block maximum: %s\n", maximum_text);
        return 2;
    }
    if (maximum != 0 && std::strcmp(argv[1], "list") == 0) {
        std::fprintf(stderr, "block maximum applies only to hive\n");
        return 2;
    }
    try {
        bool ok;
        if (std::strcmp(argv[2], "int") == 0) ok = execute<int>(argv[1], argv[2], maximum_text, maximum);
        else if (std::strcmp(argv[2], "double") == 0) ok = execute<double>(argv[1], argv[2], maximum_text, maximum);
        else if (std::strcmp(argv[2], "ordinary64") == 0) ok = execute<ordinary64>(argv[1], argv[2], maximum_text, maximum);
        else if (std::strcmp(argv[2], "align64") == 0) ok = execute<aligned64>(argv[1], argv[2], maximum_text, maximum);
        else {
            std::fprintf(stderr, "invalid type: %s\n", argv[2]);
            return 2;
        }
        return ok ? 0 : 1;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "benchmark failed: %s\n", e.what());
        return 1;
    }
}
