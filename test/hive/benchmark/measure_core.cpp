#include <hive>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <list>
#include <memory>
#include <vector>

namespace {
struct statistics {
    std::size_t bytes = 0;
    std::size_t peak = 0;
    std::size_t calls = 0;
    std::size_t bulk_unit = 0;
} stats;
volatile long long observed_sum = 0;

template<class T>
struct measured_allocator {
    using value_type = T;
    measured_allocator() = default;
    template<class U> measured_allocator(const measured_allocator<U>&) noexcept {}
    T* allocate(std::size_t n) {
        T* pointer = std::allocator<T>{}.allocate(n);
        ++stats.calls;
        stats.bytes += n * sizeof(T);
        stats.peak = std::max(stats.peak, stats.bytes);
        if (n > 1) stats.bulk_unit = std::max(stats.bulk_unit, sizeof(T));
        return pointer;
    }
    void deallocate(T* pointer, std::size_t n) noexcept {
        stats.bytes -= n * sizeof(T);
        std::allocator<T>{}.deallocate(pointer, n);
    }
    template<class U> bool operator==(const measured_allocator<U>&) const noexcept { return true; }
};

struct alignas(64) aligned_value {
    int value;
    explicit aligned_value(int v) : value(v) {}
    operator int() const noexcept { return value; }
};

template<class C>
void report(const char* name, const char* occupancy, const C& container) {
    std::array<double, 7> samples{};
    long long sum = 0;
    for (double& sample : samples) {
        const auto begin = std::chrono::steady_clock::now();
        sum = 0;
        for (const auto& value : container) sum += static_cast<int>(value);
        observed_sum = sum;
        const auto end = std::chrono::steady_clock::now();
        sample = std::chrono::duration<double, std::nano>(end - begin).count() / container.size();
    }
    std::sort(samples.begin(), samples.end());
    std::size_t capacity = container.size();
    if constexpr (requires { container.capacity(); }) capacity = container.capacity();
    std::printf("%s,%s,%zu,%zu,%zu,%zu,%zu,%.6f,%.3f,%lld\n", name, occupancy,
        container.size(), capacity, stats.bytes, stats.calls, stats.bulk_unit,
        double(stats.bytes) / container.size(), samples[3], sum);
}

template<class C>
void run(const char* name, std::size_t n, C& container) {
    stats = {};
    for (std::size_t i = 0; i != n; ++i)
        container.insert(container.end(), typename C::value_type(int(i)));
    report(name, "dense", container);
    if (observed_sum != static_cast<long long>(n * (n - 1) / 2)) std::abort();
    bool remove = false;
    for (auto it = container.begin(); it != container.end();) {
        remove = !remove;
        if (remove) it = container.erase(it);
        else ++it;
    }
    report(name, "half", container);
    if (observed_sum != static_cast<long long>((n / 2) * (n / 2))) std::abort();
}
}

int main() {
    std::puts("container,occupancy,size,capacity,live_bytes,allocations,bulk_unit_bytes,bytes_per_element,median_ns_per_element,sum");
    constexpr std::size_t n = 1000000;
    { std::list<int, measured_allocator<int>> c; run("list_int", n, c); }
    if (stats.bytes != 0) return 1;
    { std::hive<int, measured_allocator<int>> c; run("hive_int_default", n, c); }
    if (stats.bytes != 0) return 1;
    { std::hive<int, measured_allocator<int>> c(std::hive_limits{256, 256}); run("hive_int_256", n, c); }
    if (stats.bytes != 0) return 1;
    { std::hive<int, measured_allocator<int>> c(std::hive_limits{8192, 8192}); run("hive_int_8192", n, c); }
    if (stats.bytes != 0) return 1;
    { std::hive<double, measured_allocator<double>> c; run("hive_double_default", n, c); }
    if (stats.bytes != 0) return 1;
    { std::hive<aligned_value, measured_allocator<aligned_value>> c; run("hive_align64_default", 100000, c); }
    return stats.bytes != 0;
}
