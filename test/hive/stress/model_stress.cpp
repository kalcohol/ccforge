// Randomized model-based stress for the hive core-preparation branch.
// Checks structural invariants after every operation: traversal both ways,
// iterator ordering, get_iterator round trip, retained-iterator stability,
// capacity accounting against the slot allocations, per-block limits at
// allocation time, object lifetime balance, and allocation-failure rollback.
#include <hive>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <random>
#include <set>
#include <stdexcept>
#include <vector>

namespace {

std::size_t g_slot_live = 0;            // sum of live slot-array capacities
std::vector<std::size_t> g_slot_log;    // slot-array sizes allocated since reset
long g_fail_after = -1;                 // allocation fault injection countdown
long g_objects = 0;                     // live element objects
std::size_t g_cov[16];                  // coverage counters
const char* g_cov_name[16] = {"emplace_badalloc","reserve_badalloc","shrink_badalloc","shrink_realloc","reshape_badalloc","reshape_ok","splice_ok","splice_length_error","splice_badalloc","max_block_65535","erase_back","erase_range"};

template<class T>
struct tracking {
    using value_type = T;
    tracking() = default;
    template<class U> tracking(const tracking<U>&) noexcept {}
    T* allocate(std::size_t n) {
        if (g_fail_after == 0) { g_fail_after = -1; throw std::bad_alloc(); }
        if (g_fail_after > 0) --g_fail_after;
        T* const result = std::allocator<T>{}.allocate(n);
        // Representation-dependent: slots retain storage, not a live flag.
        if constexpr (requires(T& t) { t.storage; }) {
            try {
                g_slot_log.push_back(n);
            } catch (...) {
                std::allocator<T>{}.deallocate(result, n);
                throw;
            }
            g_slot_live += n;
        }
        return result;
    }
    void deallocate(T* p, std::size_t n) noexcept {
        if constexpr (requires(T& t) { t.storage; }) g_slot_live -= n;
        std::allocator<T>{}.deallocate(p, n);
    }
    template<class U> bool operator==(const tracking<U>&) const noexcept { return true; }
};

template<std::size_t Align, std::size_t Pad>
struct value {
    alignas(Align) int v;
    std::array<unsigned char, Pad> pad;
    value(int x) : v(x) { ++g_objects; }
    value(const value& o) : v(o.v) { ++g_objects; }
    value(value&& o) noexcept : v(o.v) { ++g_objects; }
    value& operator=(const value&) = default;
    value& operator=(value&&) noexcept = default;
    ~value() { --g_objects; }
    friend bool operator==(const value& a, const value& b) { return a.v == b.v; }
};

template<class Scalar>
struct scalar_value {
    Scalar v;
    scalar_value(int x) : v(static_cast<Scalar>(sizeof(Scalar) == 1 ? x & 0x3f : x)) { ++g_objects; }
    scalar_value(const scalar_value& o) : v(o.v) { ++g_objects; }
    scalar_value(scalar_value&& o) noexcept : v(o.v) { ++g_objects; }
    scalar_value& operator=(const scalar_value&) = default;
    scalar_value& operator=(scalar_value&&) noexcept = default;
    ~scalar_value() { --g_objects; }
    friend bool operator==(const scalar_value& a, const scalar_value& b) { return a.v == b.v; }
};
using small_value = scalar_value<signed char>;
static_assert(sizeof(scalar_value<int>) == sizeof(int));
static_assert(sizeof(small_value) == 1);

unsigned g_seed;
int g_step;
const char* g_op = "";

[[noreturn]] void fail(const char* what) {
    std::printf("FAIL seed=%u step=%d op=%s: %s\n", g_seed, g_step, g_op, what);
    std::fflush(stdout);
    std::abort();
}
#define CHECK(c) do { if (!(c)) fail(#c); } while (0)

template<class E>
struct harness {
    using H = std::hive<E, tracking<E>>;
    using It = typename H::iterator;
    std::mt19937 rng;
    H h;
    std::vector<std::pair<It, int>> handles;  // every live element

    explicit harness(unsigned seed) : rng(seed) {}

    std::size_t pick(std::size_t n) { return std::uniform_int_distribution<std::size_t>(0, n - 1)(rng); }
    bool chance(int percent) { return static_cast<int>(pick(100)) < percent; }

    static int val(const E& e) { return e.v; }

    std::hive_limits random_limits() {
        const auto hard = H::block_capacity_hard_limits();
        const std::size_t mins[] = {1, 1, 2, 3, 5, 8, 8, 13, 16, 40};
        std::size_t mn = mins[pick(10)];
        const std::size_t maxes[] = {mn, mn + 1, mn + 7, 64, 255, 256, 300, 1000, hard.max};
        std::size_t mx = std::max(mn, maxes[pick(9)]);
        if (chance(3)) { mn = hard.max; mx = hard.max; }
        return std::hive_limits(mn, mx);
    }

    void rebuild_handles() {
        handles.clear();
        for (auto it = h.begin(); it != h.end(); ++it) handles.push_back({it, val(*it)});
    }

    void check_alloc_log(std::hive_limits lim) {
        for (std::size_t n : g_slot_log) CHECK(lim.min <= n && n <= lim.max);
        g_slot_log.clear();
    }

    void full_check(bool sole_owner = true) {
        CHECK(h.__test_invariants());
        const std::size_t n = h.size();
        CHECK(h.capacity() >= n);
        CHECK(h.empty() == (n == 0));
        if (sole_owner) {
            CHECK(g_slot_live == h.capacity());
            CHECK(static_cast<std::size_t>(g_objects) == n);
        }
        std::vector<const E*> fwd;
        std::multiset<int> values;
        auto it = h.begin();
        typename H::const_iterator prev_it{};
        for (std::size_t i = 0; i < n; ++i, ++it) {
            CHECK(it != h.end());
            fwd.push_back(std::addressof(*it));
            values.insert(val(*it));
            if (i != 0) {
                CHECK(prev_it < it);
                CHECK((it <=> prev_it) == std::strong_ordering::greater);
                CHECK(std::prev(It(it)) == prev_it);
            }
            CHECK(it < h.end());
            CHECK(h.get_iterator(std::addressof(*it)) == it);
            CHECK(std::as_const(h).get_iterator(std::addressof(*it)) == it);
            prev_it = it;
        }
        CHECK(it == h.end());
        auto r = h.rbegin();
        for (std::size_t i = n; i > 0; --i, ++r) {
            CHECK(r != h.rend());
            CHECK(std::addressof(*r) == fwd[i - 1]);
        }
        CHECK(r == h.rend());
        CHECK(handles.size() == n);
        std::multiset<int> hv;
        std::set<const E*> addrs(fwd.begin(), fwd.end());
        CHECK(addrs.size() == n);
        for (auto& [hit, v] : handles) {
            CHECK(val(*hit) == v);
            CHECK(addrs.count(std::addressof(*hit)) == 1);
            hv.insert(v);
        }
        CHECK(hv == values);
    }

    void op_emplace() {
        g_op = "emplace";
        const int burst = chance(10) ? static_cast<int>(pick(400)) + 1 : 1;
        for (int b = 0; b < burst; ++b) {
            const int v = static_cast<int>(pick(50));
            const std::size_t before = h.size();
            const bool inject = chance(3);
            if (inject) g_fail_after = static_cast<long>(pick(3));
            try {
                It it = h.emplace(v);
                g_fail_after = -1;
                CHECK(val(*it) == val(E(v)));
                handles.push_back({it, val(*it)});
            } catch (const std::bad_alloc&) {
                ++g_cov[0];
                CHECK(h.size() == before);  // strong guarantee
            }
            g_fail_after = -1;
        }
        check_alloc_log(h.block_capacity_limits());
    }

    void op_erase() {
        g_op = "erase";
        if (handles.empty()) return;
        const std::size_t k = pick(handles.size());
        It it = handles[k].first;
        It next = std::next(it);
        const bool was_back = next == h.end();
        It r = h.erase(it);
        if (was_back) { CHECK(r == h.end()); ++g_cov[10]; }
        else CHECK(r == next);
        handles[k] = handles.back();
        handles.pop_back();
    }

    void op_erase_range() {
        g_op = "erase_range";
        const std::size_t n = h.size();
        if (n == 0) return;
        std::size_t i = pick(n + 1), j = pick(n + 1);
        if (i > j) std::swap(i, j);
        It first = std::next(h.begin(), static_cast<std::ptrdiff_t>(i));
        It last = std::next(h.begin(), static_cast<std::ptrdiff_t>(j));
        std::set<const E*> gone;
        for (It p = first; p != last; ++p) gone.insert(std::addressof(*p));
        // Classify handles before erasing; erased iterators are not used afterwards.
        std::vector<std::pair<It, int>> kept;
        for (auto& hd : handles)
            if (gone.count(std::addressof(*hd.first)) == 0) kept.push_back(hd);
        const bool to_end = j == n;
        ++g_cov[11];
        It r = h.erase(first, last);
        if (to_end) CHECK(r == h.end());
        else CHECK(r == last);
        handles = std::move(kept);
    }

    void op_reserve() {
        g_op = "reserve";
        const std::size_t n = pick(h.size() + 3000);
        const bool inject = chance(20);
        if (inject) g_fail_after = static_cast<long>(pick(4));
        try {
            h.reserve(n);
            CHECK(h.capacity() >= n);
        } catch (const std::bad_alloc&) {
            ++g_cov[1];
        }
        g_fail_after = -1;
        check_alloc_log(h.block_capacity_limits());
    }

    void op_shrink() {
        g_op = "shrink_to_fit";
        const std::size_t cap = h.capacity();
        const bool inject = chance(20);
        if (inject) g_fail_after = static_cast<long>(pick(4));
        bool threw = false;
        try { h.shrink_to_fit(); } catch (const std::bad_alloc&) { threw = true; ++g_cov[2]; }
        if (!threw && !g_slot_log.empty()) ++g_cov[3];
        g_fail_after = -1;
        CHECK(h.capacity() <= cap);
        check_alloc_log(h.block_capacity_limits());
        if (!threw) rebuild_handles();
    }

    void op_trim() {
        g_op = "trim_capacity";
        if (chance(50)) h.trim_capacity();
        else {
            const std::size_t n = pick(h.capacity() + 10);
            h.trim_capacity(n);
            CHECK(h.capacity() >= std::min(n, h.capacity()));
        }
        CHECK(h.capacity() >= h.size());
    }

    void op_reshape() {
        g_op = "reshape";
        const auto lim = random_limits();
        const auto old = h.block_capacity_limits();
        const bool inject = chance(20);
        if (inject) g_fail_after = static_cast<long>(pick(4));
        try {
            h.reshape(lim);
            g_fail_after = -1;
            CHECK(h.block_capacity_limits().min == lim.min && h.block_capacity_limits().max == lim.max);
            check_alloc_log(lim);
            ++g_cov[5];
            if (lim.max == H::block_capacity_hard_limits().max) ++g_cov[9];
        } catch (const std::bad_alloc&) {
            ++g_cov[4];
            g_fail_after = -1;
            CHECK(h.block_capacity_limits().min == old.min && h.block_capacity_limits().max == old.max);
            g_slot_log.clear();
            return;  // no effects: retained iterators must still be valid
        }
        rebuild_handles();
    }

    void op_splice() {
        g_op = "splice";
        const auto lim = random_limits();
        const auto mine = h.block_capacity_limits();
        H other(lim);
        const std::size_t count = pick(600);
        for (std::size_t i = 0; i < count; ++i) other.emplace(static_cast<int>(pick(50)));
        if (chance(30)) {
            for (auto it = other.begin(); it != other.end();) {
                if (chance(30)) it = other.erase(it); else ++it;
            }
        }
        if (chance(30)) other.reserve(other.size() + pick(500));
        g_slot_log.clear();
        std::vector<std::pair<It, int>> incoming;
        for (auto it = other.begin(); it != other.end(); ++it) incoming.push_back({it, val(*it)});
        const bool subset = mine.min <= lim.min && lim.max <= mine.max;
        const bool disjoint = lim.max < mine.min || lim.min > mine.max;
        const std::size_t before = h.size(), other_before = other.size();
        const bool inject = chance(20);
        if (inject) g_fail_after = 0;
        try {
            h.splice(other);
            g_fail_after = -1;
            CHECK(!(disjoint && other_before != 0));
            CHECK(h.size() == before + other_before);
            CHECK(other.empty());
            for (auto& x : incoming) handles.push_back(x);
            ++g_cov[6];
        } catch (const std::length_error&) {
            g_fail_after = -1;
            CHECK(!subset);
            ++g_cov[7];
            CHECK(h.size() == before && other.size() == other_before);
        } catch (const std::bad_alloc&) {
            ++g_cov[8];
            g_fail_after = -1;
            CHECK(h.size() == before && other.size() == other_before);
        }
        g_slot_log.clear();
        // other is destroyed at scope exit; checks with sole ownership follow.
    }

    void op_sort() {
        g_op = "sort";
        h.sort([](const E& a, const E& b) { return a.v < b.v; });
        rebuild_handles();
        for (std::size_t i = 1; i < handles.size(); ++i) CHECK(handles[i - 1].second <= handles[i].second);
    }

    void op_unique() {
        g_op = "unique";
        std::size_t expected = 0;
        {
            auto it = h.begin();
            if (it != h.end()) {
                int last = val(*it);
                for (++it; it != h.end(); ++it) {
                    if (val(*it) == last) ++expected; else last = val(*it);
                }
            }
        }
        CHECK(h.unique() == expected);
        rebuild_handles();
    }

    void op_copy_swap() {
        g_op = "copy_swap";
        {
            H copy(h);
            CHECK(std::equal(copy.begin(), copy.end(), h.begin(), h.end()));
            H other;
            h.swap(other);
            CHECK(h.empty());
            h.swap(other);  // handles stay valid across swaps
            g_slot_log.clear();
        }
        if (chance(30)) {
            H copy(h);
            h = copy;  // copy assignment rebuilds
            rebuild_handles();
            g_slot_log.clear();
        }
    }

    void op_clear() {
        g_op = "clear";
        h.clear();
        handles.clear();
    }

    void run(int steps) {
        for (g_step = 0; g_step < steps; ++g_step) {
            const std::size_t r = pick(100);
            if (r < 38) op_emplace();
            else if (r < 62) op_erase();
            else if (r < 66) op_erase_range();
            else if (r < 70) op_reserve();
            else if (r < 73) op_shrink();
            else if (r < 76) op_trim();
            else if (r < 80) op_reshape();
            else if (r < 85) op_splice();
            else if (r < 87) op_sort();
            else if (r < 89) op_unique();
            else if (r < 92) op_copy_swap();
            else if (r < 93) op_clear();
            else op_emplace();
            if (h.size() < 400 || g_step % 25 == 0) full_check();
        }
        full_check();
    }
};

template<class E>
void run_type(const char* name, unsigned seeds, int steps) {
    for (unsigned s = 1; s <= seeds; ++s) {
        g_seed = s;
        {
            harness<E> t(s * 7919u + 17u);
            t.run(steps);
        }
        if (g_objects != 0 || g_slot_live != 0) {
            g_op = "teardown";
            fail("leak after harness destruction");
        }
    }
    std::printf("%s: %u seeds x %d steps OK\n ", name, seeds, steps);
    for (int i = 0; i < 12; ++i) { std::printf(" %s=%zu", g_cov_name[i], g_cov[i]); g_cov[i] = 0; }
    std::printf("\n");
    std::fflush(stdout);
}

} // namespace

int main(int argc, char** argv) {
    const unsigned seeds = argc > 1 ? static_cast<unsigned>(std::atoi(argv[1])) : 40;
    const int steps = argc > 2 ? std::atoi(argv[2]) : 1500;
    run_type<scalar_value<int>>("int-sized", seeds, steps);
    run_type<small_value>("char-sized", seeds, steps);
    run_type<value<32, 4>>("align32", seeds / 2 + 1, steps);
    run_type<value<alignof(int), 60>>("64-byte", seeds / 2 + 1, steps);
    return 0;
}
