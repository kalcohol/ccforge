// Include first: the kernel must be usable without Hive or transitive GTest headers.
#include "../../../backport/cpp26/hive/detail/bounded_sort.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <new>
#include <numeric>
#include <string>
#include <type_traits>
#include <vector>

#if defined(_MSC_VER)
#include <malloc.h>
#endif

// This executable owns its allocation replacements; other Hive tests are separate.
namespace bounded_sort_allocations {
thread_local bool armed = false;
thread_local std::size_t calls = 0;

void* allocate(std::size_t size, std::size_t alignment = 0) {
    if (armed) ++calls;
    size = size == 0 ? 1 : size;
    void* result = nullptr;
    if (alignment == 0) {
        result = std::malloc(size);
    } else {
#if defined(_MSC_VER)
        result = _aligned_malloc(size, alignment);
#else
        if (size > std::numeric_limits<std::size_t>::max() - (alignment - 1))
            throw std::bad_alloc();
        result = std::aligned_alloc(alignment, (size + alignment - 1) / alignment * alignment);
#endif
    }
    if (result == nullptr) throw std::bad_alloc();
    return result;
}

void release_aligned(void* pointer) noexcept {
#if defined(_MSC_VER)
    _aligned_free(pointer);
#else
    std::free(pointer);
#endif
}

struct scope {
    std::size_t& observed;
    std::size_t before = calls;
    bool previous = armed;
    explicit scope(std::size_t& count) : observed(count) { armed = true; }
    ~scope() { observed = calls - before; armed = previous; }
};
} // namespace bounded_sort_allocations

void* operator new(std::size_t n) { return bounded_sort_allocations::allocate(n); }
void* operator new[](std::size_t n) { return bounded_sort_allocations::allocate(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void* operator new(std::size_t n, std::align_val_t a) {
    return bounded_sort_allocations::allocate(n, static_cast<std::size_t>(a));
}
void* operator new[](std::size_t n, std::align_val_t a) {
    return bounded_sort_allocations::allocate(n, static_cast<std::size_t>(a));
}
void operator delete(void* p, std::align_val_t) noexcept {
    bounded_sort_allocations::release_aligned(p);
}
void operator delete[](void* p, std::align_val_t) noexcept {
    bounded_sort_allocations::release_aligned(p);
}
void operator delete(void* p, std::size_t, std::align_val_t) noexcept {
    bounded_sort_allocations::release_aligned(p);
}
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept {
    bounded_sort_allocations::release_aligned(p);
}
void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
    try { return ::operator new(n); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept {
    try { return ::operator new[](n); } catch (...) { return nullptr; }
}
void operator delete(void* p, const std::nothrow_t&) noexcept { ::operator delete(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { ::operator delete[](p); }
void* operator new(std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept {
    try { return ::operator new(n, a); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept {
    try { return ::operator new[](n, a); } catch (...) { return nullptr; }
}
void operator delete(void* p, std::align_val_t a, const std::nothrow_t&) noexcept {
    ::operator delete(p, a);
}
void operator delete[](void* p, std::align_val_t a, const std::nothrow_t&) noexcept {
    ::operator delete[](p, a);
}

namespace {
namespace kernel = std::__forge_hive_detail;

constexpr std::uint64_t seed = 0x41b0a1ded2026119ULL;
constexpr std::size_t random_relation_count = 1'000'000;
constexpr std::array modes{0, 1, 2}; // Production, threshold 2/depth 0, threshold 2/depth 1.

struct random_bits {
    std::uint64_t state;
    std::uint64_t next() noexcept {
        state += 0x9e3779b97f4a7c15ULL;
        auto value = state;
        value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
        value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
        return value ^ (value >> 31);
    }
};

std::size_t comparison_budget(std::size_t n) {
    std::size_t logarithm = 0;
    for (auto remaining = n > 0 ? n - 1 : 0; remaining != 0; remaining >>= 1)
        ++logarithm;
    return 8 * n * logarithm + 8 * n + 64;
}

struct budget_exceeded {};
struct invalid_operand {};
struct injected_exception { std::size_t call; };

struct observation {
    kernel::__sort_test_stats stats{};
    std::size_t allocations = 0;
};

template<class T, class Compare>
void invoke(T* data, std::size_t n, Compare& compare, int mode, observation& result) {
    bounded_sort_allocations::scope allocations(result.allocations);
    if (mode == 0) {
        (void)kernel::__bounded_sort(data, n, compare);
    } else {
        (void)kernel::__bounded_sort(data, n, compare,
            kernel::__sort_test_options{2, static_cast<std::size_t>(mode - 1)}, &result.stats);
    }
}

struct totals {
    std::uint64_t cases = 0;
    std::uint64_t comparisons = 0;
    std::uint64_t partitions = 0;
    std::uint64_t heaps = 0;
    std::uint64_t insertions = 0;

    void add(std::size_t calls, const observation& result) {
        ++cases;
        comparisons += calls;
        partitions += result.stats.partitions;
        heaps += result.stats.heap_fallbacks;
        insertions += result.stats.insertions;
    }
    void report() const {
        const auto property = [](const char* name, std::uint64_t value) {
            ::testing::Test::RecordProperty(name, std::to_string(value));
        };
        property("sort_calls", cases);
        property("comparisons", comparisons);
        property("partitions", partitions);
        property("heap_fallbacks", heaps);
        property("insertions", insertions);
        ::testing::Test::RecordProperty("seed", std::to_string(seed));
    }
};

struct table_compare {
    std::uint64_t relation;
    std::size_t n;
    std::size_t calls = 0;
    std::size_t budget;
    table_compare(const table_compare&) = delete;
    table_compare(std::uint64_t relation_, std::size_t n_)
        : relation(relation_), n(n_), budget(comparison_budget(n_)) {}
    bool operator()(int left, int right) {
        if (++calls > budget) throw budget_exceeded{};
        if (left < 0 || right < 0 || static_cast<std::size_t>(left) >= n ||
            static_cast<std::size_t>(right) >= n) throw invalid_operand{};
        return ((relation >> (static_cast<std::size_t>(left) * n + right)) & 1) != 0;
    }
};

bool check_relation(std::size_t n, std::uint64_t relation, totals& counts) {
    constexpr int canary = -123456789;
    for (const int mode : modes) {
        std::array<int, 12> storage;
        storage.fill(canary);
        std::iota(storage.begin() + 2, storage.begin() + 2 + n, 0);
        table_compare compare(relation, n);
        observation observed;
        try {
            invoke(storage.data() + 2, n, compare, mode, observed);
        } catch (...) {
            ADD_FAILURE() << "Relation failed: n=" << n << " relation=" << relation
                          << " mode=" << mode << " comparisons=" << compare.calls;
            return false;
        }
        counts.add(compare.calls, observed);
        unsigned seen = 0;
        for (std::size_t i = 0; i != storage.size(); ++i) {
            if (i < 2 || i >= n + 2) {
                if (storage[i] != canary) {
                    ADD_FAILURE() << "Relation canary: n=" << n << " relation=" << relation
                                  << " mode=" << mode << " slot=" << i;
                    return false;
                }
            } else {
                const auto value = storage[i];
                if (value < 0 || static_cast<std::size_t>(value) >= n ||
                    (seen & (1u << value)) != 0) {
                    ADD_FAILURE() << "Relation permutation: n=" << n << " relation=" << relation
                                  << " mode=" << mode;
                    return false;
                }
                seen |= 1u << value;
            }
        }
        if (observed.allocations != 0 || compare.calls > compare.budget) {
            ADD_FAILURE() << "Relation allocation/comparison bound: n=" << n
                          << " relation=" << relation << " mode=" << mode;
            return false;
        }
    }
    return true;
}

class exhaustive_relations : public ::testing::TestWithParam<std::size_t> {};
TEST_P(exhaustive_relations, AllBooleanRelations) {
    const auto n = GetParam();
    const auto relations = std::uint64_t{1} << (n * n);
    totals counts;
    for (std::uint64_t relation = 0; relation != relations; ++relation)
        ASSERT_TRUE(check_relation(n, relation, counts));
    EXPECT_EQ(counts.cases, relations * modes.size());
    ::testing::Test::RecordProperty("n", std::to_string(n));
    ::testing::Test::RecordProperty("distinct_relations", std::to_string(relations));
    counts.report();
}
INSTANTIATE_TEST_SUITE_P(V3, exhaustive_relations, ::testing::Values(0, 1, 2, 3, 4));

// Each operation is invertible modulo 2^(n*n): no repeated relations among the
// million sampled indices, without a memory-intensive hash set in sanitizer runs.
std::uint64_t sampled_relation(std::uint64_t index, std::size_t n) {
    const auto bits = n * n;
    const auto mask = bits == 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << bits) - 1;
    auto value = (index + seed + n) & mask;
    value = (value ^ (value >> 11)) & mask;
    value = (value * 0xbf58476d1ce4e5b9ULL) & mask;
    value = (value ^ (value >> 7)) & mask;
    value = (value * 0x94d049bb133111ebULL) & mask;
    return (value ^ (value >> 13)) & mask;
}

class sampled_relations : public ::testing::TestWithParam<std::size_t> {};
TEST_P(sampled_relations, MillionDistinctBooleanRelations) {
    const auto n = GetParam();
    totals counts;
    for (std::size_t i = 0; i != random_relation_count; ++i)
        ASSERT_TRUE(check_relation(n, sampled_relation(i, n), counts)) << "sample=" << i;
    EXPECT_EQ(counts.cases, random_relation_count * modes.size());
    ::testing::Test::RecordProperty("n", std::to_string(n));
    ::testing::Test::RecordProperty("distinct_relations", std::to_string(random_relation_count));
    counts.report();
}
INSTANTIATE_TEST_SUITE_P(V3, sampled_relations, ::testing::Values(5, 6, 7, 8));

struct record16 {
    int key;
    std::uint32_t serial;
    std::uint64_t payload;
    bool operator==(const record16&) const = default;
};
struct address_entry {
    const int* address;
    std::size_t serial;
    bool operator==(const address_entry&) const = default;
};
static_assert(sizeof(record16) == 16);
static_assert(std::is_trivially_copyable_v<record16>);
static_assert(std::is_trivially_copyable_v<address_entry>);

int key_of(int value) { return value; }
int key_of(const record16& value) { return value.key; }
int key_of(const address_entry& value) { return *value.address; }

template<class T>
std::vector<T> make_values(const std::vector<int>& keys) {
    std::vector<T> values;
    values.reserve(keys.size());
    for (std::size_t i = 0; i != keys.size(); ++i) {
        if constexpr (std::is_same_v<T, int>) values.push_back(keys[i]);
        else if constexpr (std::is_same_v<T, record16>)
            values.push_back({keys[i], static_cast<std::uint32_t>(i),
                              (seed ^ i) * 0x9e3779b97f4a7c15ULL});
        else values.push_back({&keys[i], i});
    }
    return values;
}

template<class T>
T guard_value() {
    if constexpr (std::is_same_v<T, int>) return -123456789;
    else if constexpr (std::is_same_v<T, record16>) return {-123456789, 0xfefefefe, seed};
    else {
        static constexpr int guard_key = -123456789;
        return {&guard_key, std::numeric_limits<std::size_t>::max()};
    }
}

template<class T>
struct guarded_buffer {
    static constexpr std::size_t guards = 4;
    std::vector<T> storage;
    std::size_t n;
    explicit guarded_buffer(const std::vector<T>& input)
        : storage(input.size() + guards * 2, guard_value<T>()), n(input.size()) {
        std::copy(input.begin(), input.end(), data());
    }
    T* data() { return storage.data() + guards; }
    bool intact() const {
        for (std::size_t i = 0; i != guards; ++i)
            if (!(storage[i] == guard_value<T>()) ||
                !(storage[n + guards + i] == guard_value<T>())) return false;
        return true;
    }
};

template<class T>
bool same_multiset(const std::vector<T>& original, const T* result) {
    if constexpr (std::is_same_v<T, int>) {
        std::vector<int> expected = original;
        std::vector<int> actual(result, result + original.size());
        std::sort(expected.begin(), expected.end());
        std::sort(actual.begin(), actual.end());
        return expected == actual;
    } else {
        std::vector<unsigned char> seen(original.size(), 0);
        for (std::size_t i = 0; i != original.size(); ++i) {
            const auto serial = static_cast<std::size_t>(result[i].serial);
            if (serial >= original.size() || seen[serial] != 0 ||
                !(result[i] == original[serial])) return false;
            seen[serial] = 1;
        }
        return true;
    }
}

enum class relation_kind { less, less_equal, always_true, always_false, random,
                           cycle, incomparable, changing };
constexpr std::array invalid_kinds{relation_kind::less_equal, relation_kind::always_true,
    relation_kind::always_false, relation_kind::random, relation_kind::cycle,
    relation_kind::incomparable, relation_kind::changing};

struct controlled_compare {
    relation_kind kind;
    std::size_t budget;
    bool descending = false;
    std::size_t calls = 0;
    std::size_t throw_at = 0;
    random_bits random{seed};
    controlled_compare(relation_kind k, std::size_t n, bool reverse = false)
        : kind(k), budget(comparison_budget(n)), descending(reverse) {}
    controlled_compare(const controlled_compare&) = delete;

    template<class T>
    bool operator()(const T& left, const T& right) {
        if (++calls > budget) throw budget_exceeded{};
        if (calls == throw_at) throw injected_exception{calls};
        const int a = key_of(left), b = key_of(right);
        switch (kind) {
        case relation_kind::less: return descending ? b < a : a < b;
        case relation_kind::less_equal: return a <= b;
        case relation_kind::always_true: return true;
        case relation_kind::always_false: return false;
        case relation_kind::random: return (random.next() & 1) != 0;
        case relation_kind::cycle: {
            const int x = (a % 3 + 3) % 3, y = (b % 3 + 3) % 3;
            return (x + 1) % 3 == y;
        }
        // 0 is incomparable with everything, while 1 < 2: equivalence is not transitive.
        case relation_kind::incomparable: return a != 0 && b != 0 && a < b;
        case relation_kind::changing: return (calls % 7) < 3 ? a < b : b <= a;
        }
        return false;
    }
};

enum class pattern { random, ascending, descending, binary, sixteen,
                     organ_pipe, sawtooth, median_three_unfavorable };
constexpr std::array patterns{pattern::random, pattern::ascending, pattern::descending,
    pattern::binary, pattern::sixteen, pattern::organ_pipe, pattern::sawtooth,
    pattern::median_three_unfavorable};

std::vector<int> make_keys(std::size_t n, pattern form) {
    std::vector<int> keys(n);
    random_bits random{seed ^ n};
    for (std::size_t i = 0; i != n; ++i) {
        switch (form) {
        case pattern::random: keys[i] = static_cast<int>(random.next() % 2000001) - 1000000; break;
        case pattern::ascending: keys[i] = static_cast<int>(i); break;
        case pattern::descending: keys[i] = static_cast<int>(n - i); break;
        case pattern::binary: keys[i] = static_cast<int>(random.next() % 2); break;
        case pattern::sixteen: keys[i] = static_cast<int>(random.next() % 16); break;
        case pattern::organ_pipe: keys[i] = static_cast<int>(std::min(i, n - 1 - i)); break;
        case pattern::sawtooth: keys[i] = static_cast<int>(i % 37); break;
        case pattern::median_three_unfavorable: keys[i] = static_cast<int>(i); break;
        }
    }
    if (form == pattern::median_three_unfavorable) {
        // Interleave lower/upper odd ranks, then append the even ranks (Musser's
        // median-of-three unfavorable arrangement). Tail ranks handle n % 4.
        const auto whole = n - n % 4;
        std::size_t out = 0;
        for (std::size_t odd = 1; odd < whole / 2; odd += 2) {
            keys[out++] = static_cast<int>(odd);
            keys[out++] = static_cast<int>(odd + whole / 2);
        }
        for (std::size_t even = 2; even <= whole; even += 2)
            keys[out++] = static_cast<int>(even);
        for (std::size_t i = whole; i != n; ++i) keys[i] = static_cast<int>(i + 1);
    }
    return keys;
}

template<class T>
bool check_valid(const std::vector<int>& keys, int mode, bool descending, totals& counts) {
    const auto input = make_values<T>(keys);
    guarded_buffer<T> buffer(input);
    auto expected = input;
    const auto order = [descending](const T& a, const T& b) {
        return descending ? key_of(b) < key_of(a) : key_of(a) < key_of(b);
    };
    std::sort(expected.begin(), expected.end(), order);
    controlled_compare compare(relation_kind::less, input.size(), descending);
    observation observed;
    try { invoke(buffer.data(), input.size(), compare, mode, observed); }
    catch (...) { ADD_FAILURE() << "Valid comparator threw; comparisons=" << compare.calls; return false; }
    counts.add(compare.calls, observed);
    if (!buffer.intact() || observed.allocations != 0 ||
        !std::is_sorted(buffer.data(), buffer.data() + keys.size(), order) ||
        !same_multiset(input, buffer.data())) {
        ADD_FAILURE() << "Valid sort failed canary/allocation/order/permutation control";
        return false;
    }
    for (std::size_t i = 0; i != input.size(); ++i) {
        if (key_of(buffer.data()[i]) != key_of(expected[i])) {
            ADD_FAILURE() << "Differential key mismatch at " << i;
            return false;
        }
    }
    return true;
}

using value_types = ::testing::Types<int, address_entry, record16>;
template<class T> class bounded_sort_valid : public ::testing::Test {};
TYPED_TEST_SUITE(bounded_sort_valid, value_types);
TYPED_TEST(bounded_sort_valid, V1DifferentialAllSizesTypesAndPatterns) {
    std::vector<std::size_t> sizes(66);
    std::iota(sizes.begin(), sizes.end(), 0);
    // Includes production threshold 24 +/- 1 and common threshold boundaries.
    const std::array<std::size_t, 20> larger{95, 96, 97, 127, 128, 129, 255, 256, 257,
        1023, 1024, 1025, 4095, 4096, 4097, 65535, 65536, 65537, 262144, 1000000};
    sizes.insert(sizes.end(), larger.begin(), larger.end());
    random_bits random{seed};
    for (int i = 0; i != 6; ++i) sizes.push_back(66 + random.next() % (1000000 - 65));
    totals counts;
    for (const auto n : sizes) for (const auto form : patterns) {
        const auto keys = make_keys(n, form);
        for (const bool descending : {false, true}) for (const int mode : modes) {
            // Large differential cases use production settings; forced paths have
            // exhaustive small controls and the separate large V2 budget controls.
            if (n > 65 && mode != 0) continue;
            ASSERT_TRUE(check_valid<TypeParam>(keys, mode, descending, counts))
                << "n=" << n << " pattern=" << static_cast<int>(form)
                << " mode=" << mode << " descending=" << descending;
        }
    }
    ::testing::Test::RecordProperty("size_cases", std::to_string(sizes.size()));
    ::testing::Test::RecordProperty("patterns", std::to_string(patterns.size()));
    ::testing::Test::RecordProperty("max_n", "1000000");
    counts.report();
}

TYPED_TEST(bounded_sort_valid, V2InvalidComparatorsAllPatternsAndBudgets) {
    constexpr std::array<std::size_t, 21> sizes{0, 1, 2, 3, 4, 5, 8, 16, 23, 24, 25,
        31, 32, 33, 63, 64, 65, 129, 1024, 4096, 65536};
    totals counts;
    for (const auto kind : invalid_kinds) for (const auto n : sizes)
        for (const auto form : patterns) for (const int mode : modes) {
            const auto keys = make_keys(n, form);
            const auto input = make_values<TypeParam>(keys);
            guarded_buffer<TypeParam> buffer(input);
            controlled_compare compare(kind, n);
            observation observed;
            try { invoke(buffer.data(), n, compare, mode, observed); }
            catch (...) {
                FAIL() << "Invalid comparator exceeded budget/threw: kind=" << static_cast<int>(kind)
                       << " n=" << n << " pattern=" << static_cast<int>(form)
                       << " mode=" << mode << " comparisons=" << compare.calls;
            }
            ASSERT_TRUE(buffer.intact()) << "kind=" << static_cast<int>(kind) << " n=" << n;
            ASSERT_TRUE(same_multiset(input, buffer.data()))
                << "kind=" << static_cast<int>(kind) << " n=" << n << " mode=" << mode;
            ASSERT_EQ(observed.allocations, 0u);
            ASSERT_LE(compare.calls, comparison_budget(n));
            counts.add(compare.calls, observed);
        }
    EXPECT_EQ(counts.cases, invalid_kinds.size() * sizes.size() * patterns.size() * modes.size());
    ::testing::Test::RecordProperty("comparator_kinds", std::to_string(invalid_kinds.size()));
    ::testing::Test::RecordProperty("size_cases", std::to_string(sizes.size()));
    ::testing::Test::RecordProperty("patterns", std::to_string(patterns.size()));
    counts.report();
}

TYPED_TEST(bounded_sort_valid, V2EveryComparisonThrowPoint) {
    constexpr std::array<std::size_t, 8> sizes{0, 1, 2, 3, 8, 25, 65, 129};
    std::uint64_t injected = 0;
    totals counts;
    for (int k = 0; k != 8; ++k) for (const auto n : sizes)
        for (const auto form : {pattern::random, pattern::median_three_unfavorable})
            for (const int mode : modes) {
                const auto kind = static_cast<relation_kind>(k);
                const auto keys = make_keys(n, form);
                const auto input = make_values<TypeParam>(keys);
                guarded_buffer<TypeParam> baseline(input);
                controlled_compare reference(kind, n);
                observation baseline_observed;
                ASSERT_NO_THROW(invoke(baseline.data(), n, reference, mode, baseline_observed));
                ASSERT_TRUE(baseline.intact());
                ASSERT_TRUE(same_multiset(input, baseline.data()));
                ASSERT_EQ(baseline_observed.allocations, 0u);
                counts.add(reference.calls, baseline_observed);
                // Include calls+1: the throwing adapter must also complete normally
                // when the selected throw point is never reached.
                for (std::size_t point = 1; point <= reference.calls + 1; ++point) {
                    guarded_buffer<TypeParam> buffer(input);
                    controlled_compare compare(kind, n);
                    compare.throw_at = point;
                    observation observed;
                    bool caught = false;
                    try { invoke(buffer.data(), n, compare, mode, observed); }
                    catch (const injected_exception& error) {
                        caught = true;
                        ASSERT_EQ(error.call, point);
                        ASSERT_EQ(compare.calls, point);
                        ++injected;
                    } catch (...) {
                        FAIL() << "Unexpected exception: kind=" << k << " n=" << n
                               << " mode=" << mode << " throw_at=" << point;
                    }
                    ASSERT_EQ(caught, point <= reference.calls)
                        << "kind=" << k << " n=" << n << " mode=" << mode;
                    ASSERT_TRUE(buffer.intact());
                    ASSERT_EQ(observed.allocations, 0u);
                    if (!caught) ASSERT_TRUE(same_multiset(input, buffer.data()));
                    // On exception only the bounds contract applies; the kernel's
                    // temporary contents need not retain their original multiset.
                    counts.add(compare.calls, observed);
                }
            }
    ::testing::Test::RecordProperty("injected_exceptions", std::to_string(injected));
    counts.report();
}

bool check_duplicate_word(const std::array<int, 8>& word, std::size_t n, totals& counts) {
    const std::vector<int> input(word.begin(), word.begin() + n);
    for (const int mode : modes) for (const bool descending : {false, true})
        if (!check_valid<int>(input, mode, descending, counts)) return false;
    return true;
}

// Enumerate every ordered equality pattern exactly once: assign an existing rank,
// or insert a new rank at any position and shift the preceding ranks accordingly.
// This covers all duplicate inputs up to order-preserving relabeling, rather than
// just binary words or nondecreasing multisets. Strict comparisons see only ranks.
bool enumerate_words(std::array<int, 8>& word, std::size_t n, std::size_t position,
                     int ranks, totals& counts, std::uint64_t& words,
                     std::uint64_t& duplicates) {
    if (position == n) {
        ++words;
        if (static_cast<std::size_t>(ranks) < n) ++duplicates;
        return check_duplicate_word(word, n, counts);
    }
    for (int rank = 0; rank < ranks; ++rank) {
        word[position] = rank;
        if (!enumerate_words(word, n, position + 1, ranks, counts, words, duplicates)) return false;
    }
    for (int rank = 0; rank <= ranks; ++rank) {
        for (std::size_t i = 0; i != position; ++i) if (word[i] >= rank) ++word[i];
        word[position] = rank;
        if (!enumerate_words(word, n, position + 1, ranks + 1, counts, words, duplicates)) return false;
        for (std::size_t i = 0; i != position; ++i) if (word[i] > rank) --word[i];
    }
    return true;
}

class duplicate_words : public ::testing::TestWithParam<std::size_t> {};
TEST_P(duplicate_words, V3AllOrderedEqualityPatterns) {
    constexpr std::array<std::uint64_t, 9> ordered_bell{1, 1, 3, 13, 75, 541, 4683, 47293, 545835};
    constexpr std::array<std::uint64_t, 9> factorial{1, 1, 2, 6, 24, 120, 720, 5040, 40320};
    const auto n = GetParam();
    std::array<int, 8> word{};
    totals counts;
    std::uint64_t words = 0, duplicates = 0;
    ASSERT_TRUE(enumerate_words(word, n, 0, 0, counts, words, duplicates)) << "n=" << n;
    EXPECT_EQ(words, ordered_bell[n]);
    EXPECT_EQ(duplicates, ordered_bell[n] - factorial[n]);
    EXPECT_EQ(counts.cases, words * modes.size() * 2);
    ::testing::Test::RecordProperty("n", std::to_string(n));
    ::testing::Test::RecordProperty("distinct_rank_words", std::to_string(words));
    ::testing::Test::RecordProperty("duplicate_rank_words", std::to_string(duplicates));
    counts.report();
}
INSTANTIATE_TEST_SUITE_P(V3, duplicate_words, ::testing::Values(0, 1, 2, 3, 4, 5, 6, 7, 8));

TEST(hive_bounded_sort, NullAndSingletonNeverCompare) {
    totals counts;
    for (const int mode : modes) {
        for (const std::size_t n : {0u, 1u}) {
            controlled_compare compare(relation_kind::less, n);
            compare.throw_at = 1;
            observation observed;
            ASSERT_NO_THROW(invoke(static_cast<int*>(nullptr), n, compare, mode, observed));
            EXPECT_EQ(compare.calls, 0u);
            EXPECT_EQ(observed.allocations, 0u);
            EXPECT_EQ(observed.stats.partitions, 0u);
            EXPECT_EQ(observed.stats.heap_fallbacks, 0u);
            EXPECT_EQ(observed.stats.insertions, 0u);
            counts.add(compare.calls, observed);
        }
        int singleton = 42;
        controlled_compare compare(relation_kind::less, 1);
        observation observed;
        ASSERT_NO_THROW(invoke(&singleton, 1, compare, mode, observed));
        EXPECT_EQ(singleton, 42);
        EXPECT_EQ(compare.calls, 0u);
    }
    counts.report();
}

TEST(hive_bounded_sort, ForcedDepthAndInsertionCountersAreLive) {
    const auto keys = make_keys(257, pattern::random);
    for (const int mode : {1, 2}) {
        guarded_buffer<int> buffer(keys);
        controlled_compare compare(relation_kind::less, keys.size());
        observation observed;
        ASSERT_NO_THROW(invoke(buffer.data(), keys.size(), compare, mode, observed));
        EXPECT_TRUE(buffer.intact());
        EXPECT_TRUE(std::is_sorted(buffer.data(), buffer.data() + keys.size()));
        EXPECT_TRUE(same_multiset(keys, buffer.data()));
        EXPECT_EQ(observed.allocations, 0u);
        EXPECT_EQ(observed.stats.partitions, mode == 1 ? 0u : 1u);
        EXPECT_GT(observed.stats.heap_fallbacks, 0u);
        if (mode == 1) EXPECT_EQ(observed.stats.insertions, 0u);
    }
    // A two-element range is always monotone. Use a nonmonotone three-element
    // range with threshold 3 to demonstrate the insertion counter independently.
    std::array<int, 3> small{2, 0, 1};
    controlled_compare compare(relation_kind::less, small.size());
    observation observed;
    {
        bounded_sort_allocations::scope allocations(observed.allocations);
        ASSERT_NO_THROW((void)kernel::__bounded_sort(small.data(), small.size(), compare,
            kernel::__sort_test_options{3, 8}, &observed.stats));
    }
    EXPECT_TRUE(std::is_sorted(small.begin(), small.end()));
    EXPECT_GT(observed.stats.insertions, 0u);
    EXPECT_EQ(observed.stats.partitions, 0u);
    EXPECT_EQ(observed.stats.heap_fallbacks, 0u);
    EXPECT_EQ(observed.allocations, 0u);
}

TEST(hive_bounded_sort, OrderedInputReportsNoWritebackNeeded) {
    auto compare = [](int a, int b) { return a < b; };
    std::array<int, 7> ordered{1, 1, 2, 3, 3, 8, 10};
    const auto original = ordered;
    EXPECT_FALSE(kernel::__bounded_sort(ordered.data(), ordered.size(), compare));
    EXPECT_EQ(ordered, original);
    std::reverse(ordered.begin(), ordered.end());
    EXPECT_TRUE(kernel::__bounded_sort(ordered.data(), ordered.size(), compare));
    EXPECT_EQ(ordered, original);
    std::array<int, 3> unordered{3, 1, 2};
    EXPECT_TRUE(kernel::__bounded_sort(unordered.data(), unordered.size(), compare));
    EXPECT_TRUE(std::is_sorted(unordered.begin(), unordered.end()));
}

TEST(hive_bounded_sort, SupportsDeletedCopyAndMoveConstructors) {
    struct value {
        int key;
        explicit value(int k) : key(k) {}
        value(const value&) = delete;
        value(value&&) = delete;
        value& operator=(const value&) = default;
    };
    static_assert(std::is_trivially_copyable_v<value>);
    for (const int mode : modes) {
        std::array<value, 5> values{value{3}, value{1}, value{4}, value{0}, value{2}};
        auto compare = [](const value& a, const value& b) { return a.key < b.key; };
        observation observed;
        ASSERT_NO_THROW(invoke(values.data(), values.size(), compare, mode, observed));
        for (std::size_t i = 0; i < values.size(); ++i) EXPECT_EQ(values[i].key, i);
        EXPECT_EQ(observed.allocations, 0u);
    }
}

TEST(hive_bounded_sort, F5CountingComparatorProductionAndForcedPaths) {
    constexpr std::array<std::size_t, 13> sizes{0, 1, 2, 23, 24, 25, 65, 257, 1024,
        4096, 65536, 262144, 1000000};
    totals counts;
    for (const auto n : sizes) for (const int mode : modes)
        for (const auto form : {pattern::ascending, pattern::descending,
                               pattern::binary, pattern::median_three_unfavorable}) {
            auto input = make_keys(n, form);
            ASSERT_TRUE(check_valid<int>(input, mode, false, counts))
                << "n=" << n << " mode=" << mode << " pattern=" << static_cast<int>(form);
        }
    for (const auto n : sizes) for (const int mode : modes) {
        const std::vector<int> equal(n, 7);
        ASSERT_TRUE(check_valid<int>(equal, mode, false, counts)) << "all_equal n=" << n;
    }
    ::testing::Test::RecordProperty("size_cases", std::to_string(sizes.size()));
    ::testing::Test::RecordProperty("patterns", "5");
    counts.report();
}
} // namespace
