#include <hive>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <limits>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

namespace {
using clock_type = std::chrono::steady_clock;
constexpr std::uint64_t seed = 1;
static_assert(sizeof(int) == 4 && std::numeric_limits<int>::max() == 2147483647);

struct options {
    std::string_view container;
    std::string_view workload;
    bool smoke = false;
    std::size_t smoke_size = 257;
    std::size_t smoke_samples = 3;

    std::size_t size() const {
        if (smoke) return smoke_size;
        if (workload == "S5" || workload == "S6") return 200000;
        return workload == "S7" ? 1000 : 1000000;
    }
    std::size_t samples() const {
        return smoke ? smoke_samples : (workload == "S7" ? 2001 : 31);
    }
    bool p0_workload() const {
        return workload != "T16" && workload != "T32";
    }
    bool p1_workload() const {
        return p0_workload() && workload != "S8";
    }
};

template<std::size_t Bytes>
struct trivial_record {
    int key{};
    std::array<unsigned char, Bytes - sizeof(int)> payload{};
    bool operator==(const trivial_record&) const = default;
};

struct nontrivial16 {
    int key{};
    std::array<unsigned char, 12> payload{};
    nontrivial16() = default;
    nontrivial16(const nontrivial16& other) : key(other.key), payload(other.payload) {}
    nontrivial16& operator=(const nontrivial16&) = default;
    bool operator==(const nontrivial16&) const = default;
};

static_assert(sizeof(trivial_record<16>) == 16 && sizeof(trivial_record<32>) == 32);
static_assert(sizeof(trivial_record<64>) == 64 && sizeof(nontrivial16) == 16);
static_assert(std::is_trivially_copyable_v<trivial_record<16>>);
static_assert(std::is_trivially_copyable_v<trivial_record<32>>);
static_assert(std::is_trivially_copyable_v<trivial_record<64>>);
static_assert(!std::is_trivially_copyable_v<nontrivial16>);

struct allocation_state {
    std::size_t live = 0;
    std::size_t peak = 0;
    std::size_t calls = 0;
};

// Rebound allocators share this state, including allocations of internal tables.
template<class T>
struct measured_allocator {
    using value_type = T;
    allocation_state* state;

    explicit measured_allocator(allocation_state& s) noexcept : state(&s) {}
    template<class U>
    measured_allocator(const measured_allocator<U>& other) noexcept : state(other.state) {}

    T* allocate(std::size_t n) {
        T* p = std::allocator<T>{}.allocate(n);
        state->live += n * sizeof(T);
        state->peak = std::max(state->peak, state->live);
        ++state->calls;
        return p;
    }
    void deallocate(T* p, std::size_t n) noexcept {
        state->live -= n * sizeof(T);
        std::allocator<T>{}.deallocate(p, n);
    }
    template<class U>
    bool operator==(const measured_allocator<U>& other) const noexcept {
        return state == other.state;
    }
};

struct key_less {
    template<class T>
    bool operator()(const T& a, const T& b) const noexcept {
        if constexpr (std::is_same_v<T, int> || std::is_same_v<T, std::string>)
            return a < b;
        else
            return a.key < b.key;
    }
};

// Canonicalization includes every payload byte; key-equivalent elements may move.
struct full_less {
    template<class T>
    bool operator()(const T& a, const T& b) const noexcept {
        if constexpr (std::is_same_v<T, int> || std::is_same_v<T, std::string>) {
            return a < b;
        } else {
            if (a.key != b.key) return a.key < b.key;
            return a.payload < b.payload;
        }
    }
};

int random_key(std::mt19937_64& rng) {
    // Uniform over [0, INT32_MAX], independent of library distribution mappings.
    return static_cast<int>(rng() & 0x7fffffffULL);
}

template<class T>
T random_value(std::mt19937_64& rng) {
    if constexpr (std::is_same_v<T, int>) {
        return random_key(rng);
    } else if constexpr (std::is_same_v<T, std::string>) {
        std::string value(32, 'a');
        for (char& c : value) c = static_cast<char>('a' + (rng() % 26));
        return value;
    } else {
        T value;
        value.key = random_key(rng);
        std::uint64_t word = 0;
        for (std::size_t i = 0; i < value.payload.size(); ++i) {
            if (i % 8 == 0) word = rng();
            value.payload[i] = static_cast<unsigned char>(word & 0xffU);
            word >>= 8;
        }
        return value;
    }
}

template<class C>
void add(C& container, const typename C::value_type& value) {
    if constexpr (requires { container.push_back(value); })
        container.push_back(value);
    else
        container.insert(value);
}

template<class C>
void populate(C& container, const options& opt) {
    using T = typename C::value_type;
    std::mt19937_64 rng(seed);
    for (std::size_t i = 0; i < opt.size(); ++i) {
        if constexpr (std::is_same_v<T, int>) {
            if (opt.workload == "S3") add(container, static_cast<int>(i));
            else if (opt.workload == "S4") add(container, static_cast<int>(rng() & 15U));
            else add(container, random_value<T>(rng));
        } else {
            add(container, random_value<T>(rng));
        }
    }
    if (opt.workload == "S2") {
        for (int round = 0; round < 3; ++round) {
            std::size_t removed = 0;
            for (auto it = container.begin(); it != container.end();) {
                if (rng() & 1U) {
                    it = container.erase(it);
                    ++removed;
                } else {
                    ++it;
                }
            }
            for (std::size_t i = 0; i < removed; ++i) add(container, random_value<T>(rng));
        }
    }
}

template<class T, bool Vector, class Allocator>
auto make_input(const options& opt, const Allocator& allocator) {
    if constexpr (Vector) {
        std::vector<T, Allocator> container(allocator);
        container.reserve(opt.size());
        if (opt.workload == "S2") {
            // Stable vector erasure would change later RNG-to-element assignments.
            // Copy the public hive traversal so vector sorts the identical input.
            std::hive<T> source;
            populate(source, opt);
            container.assign(source.begin(), source.end());
        } else {
            populate(container, opt);
        }
        return container;
    } else {
        std::hive<T, Allocator> container(allocator);
        populate(container, opt);
        return container;
    }
}

struct checksum {
    std::uint64_t value = 14695981039346656037ULL;
    void byte(unsigned char b) {
        value ^= b;
        value *= 1099511628211ULL;
    }
    void integer(std::uint64_t word, std::size_t bytes) {
        for (std::size_t i = 0; i < bytes; ++i) {
            byte(static_cast<unsigned char>(word & 0xffU));
            word >>= 8;
        }
    }
    template<class T>
    void append(const T& item) {
        if constexpr (std::is_same_v<T, int>) {
            integer(static_cast<std::uint32_t>(item), 4);
        } else if constexpr (std::is_same_v<T, std::string>) {
            integer(item.size(), 8);
            for (unsigned char b : item) byte(b);
        } else {
            integer(static_cast<std::uint32_t>(item.key), 4);
            for (unsigned char b : item.payload) byte(b);
        }
    }
};

template<class T>
struct reference {
    std::vector<T> values;
    std::uint64_t hash;
};

template<class T, class C>
reference<T> reference_from(const C& container) {
    std::vector<T> values(container.begin(), container.end());
    std::sort(values.begin(), values.end(), full_less{});
    checksum hash;
    for (const auto& value : values) hash.append(value);
    return {std::move(values), hash.value};
}

template<class T, bool Vector>
reference<T> make_reference(const options& opt) {
    auto container = make_input<T, Vector>(opt, std::allocator<T>{});
    return reference_from<T>(container);
}

template<class C, class T>
void validate(const C& container, const reference<T>& expected) {
    if (container.size() != expected.values.size())
        throw std::runtime_error("sort changed the size");
    if (!std::is_sorted(container.begin(), container.end(), key_less{}))
        throw std::runtime_error("sort result is not ordered by key");
    std::vector<T> actual(container.begin(), container.end());
    std::sort(actual.begin(), actual.end(), full_less{});
    if (actual != expected.values)
        throw std::runtime_error("sort changed the full-payload multiset");
    checksum hash;
    for (const auto& value : actual) hash.append(value);
    if (hash.value != expected.hash)
        throw std::runtime_error("sort full-payload checksum mismatch");
}

template<bool Vector, class C>
void sort_input(C& container) {
    if constexpr (Vector)
        std::sort(container.begin(), container.end(), key_less{});
    else
        container.sort(key_less{});
}

struct memory_result {
    std::size_t peak;
    std::size_t calls;
};

template<class T, bool Vector>
memory_result measure_memory(const options& opt) {
    allocation_state state;
    memory_result result{};
    {
        auto container = make_input<T, Vector>(opt, measured_allocator<T>{state});
        // Churn order can depend on allocator-specific block layout. Compare
        // this probe with its own complete input, not the timing allocator's.
        const auto expected = reference_from<T>(container);
        const auto capacity = container.capacity();
        // Keep live accounting intact for deallocation, but exclude resident bytes.
        const std::size_t baseline = state.live;
        state.peak = baseline;
        state.calls = 0;
        sort_input<Vector>(container);
        result = {state.peak - baseline, state.calls};
        if (state.live != baseline)
            throw std::runtime_error("sort did not release its temporary allocations");
        if (container.capacity() != capacity)
            throw std::runtime_error("sort changed the capacity");
        validate(container, expected);
    }
    if (state.live != 0) throw std::runtime_error("allocator reports a leak");
    return result;
}

template<class T, bool Vector>
void execute(const options& opt, const char* type) {
    const auto expected = make_reference<T, Vector>(opt);
    std::vector<double> samples(opt.samples());
    for (std::size_t run = 0; run <= opt.samples(); ++run) {
        // Reconstruct with seed 1 even for the warmup; setup/validation are untimed.
        auto container = make_input<T, Vector>(opt, std::allocator<T>{});
        const auto capacity = container.capacity();
        const auto begin = clock_type::now();
        sort_input<Vector>(container);
        const auto end = clock_type::now();
        if (run != 0)
            samples[run - 1] = std::chrono::duration<double, std::nano>(end - begin).count();
        if (container.capacity() != capacity)
            throw std::runtime_error("sort changed the capacity");
        validate(container, expected);
    }
    for (std::size_t i = 0; i < samples.size(); ++i)
        std::fprintf(stderr, "sample_ns,%zu,%.0f\n", i, samples[i]);
    std::sort(samples.begin(), samples.end());
    // A separate untimed sort avoids charging counter/rebind bookkeeping to latency.
    const auto memory = measure_memory<T, Vector>(opt);
    std::puts("container,workload,type,element_bytes,p0_workload,p1_workload,smoke,size,seed,warmups,samples,timing_allocator,min_ns,median_ns,memory_allocator,memory_probe_sorts,temporary_peak_bytes,temporary_bytes_per_element,temporary_allocation_calls,payload_checksum");
    std::printf("%.*s,%.*s,%s,%zu,%s,%s,%s,%zu,%llu,1,%zu,std_allocator,%.0f,%.0f,rebind_counter,1,%zu,%.6f,%zu,%016llx\n",
        static_cast<int>(opt.container.size()), opt.container.data(),
        static_cast<int>(opt.workload.size()), opt.workload.data(), type, sizeof(T),
        opt.p0_workload() ? "true" : "false", opt.p1_workload() ? "true" : "false",
        opt.smoke ? "true" : "false",
        opt.size(), static_cast<unsigned long long>(seed), opt.samples(),
        samples.front(), samples[samples.size() / 2], memory.peak,
        static_cast<double>(memory.peak) / opt.size(), memory.calls,
        static_cast<unsigned long long>(expected.hash));
}

template<bool Vector>
void dispatch(const options& opt) {
    if (opt.workload == "S5") execute<trivial_record<64>, Vector>(opt, "trivial64");
    else if (opt.workload == "S6") execute<std::string, Vector>(opt, "string32");
    else if (opt.workload == "S8") execute<nontrivial16, Vector>(opt, "nontrivial16");
    else if (opt.workload == "T16") execute<trivial_record<16>, Vector>(opt, "trivial16");
    else if (opt.workload == "T32") execute<trivial_record<32>, Vector>(opt, "trivial32");
    else execute<int, Vector>(opt, "int");
}

void usage(const char* program) {
    std::fprintf(stderr,
        "usage: %s {hive|vector} {S1|S2|S3|S4|S5|S6|S7|S8|T16|T32}\n"
        "       [--smoke [--smoke-size N] [--smoke-samples ODD_N]]\n"
        "Production: one warmup, 31 samples (S7: 2001); taskbook sizes.\n"
        "Smoke: one warmup, size 257, 3 samples unless explicitly overridden.\n"
        "P0 workloads: S1-S8; P1 workloads: S1-S7 (S8 remains subject to P0).\n"
        "T16/T32 are study-only, with production size 1000000.\n"
        "Timing uses std::allocator; one extra untimed sort measures rebind bytes.\n"
        "Pin CPU and record compiler/topology/load/batch ordering externally.\n", program);
}

std::size_t positive_number(std::string_view text) {
    std::size_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || value == 0)
        throw std::invalid_argument("expected a positive integer");
    return value;
}

options parse_options(int argc, char** argv) {
    if (argc < 3) throw std::invalid_argument("container and workload are required");
    options opt{argv[1], argv[2]};
    if (opt.container != "hive" && opt.container != "vector")
        throw std::invalid_argument("unknown container");
    constexpr std::array workloads{"S1", "S2", "S3", "S4", "S5", "S6", "S7", "S8", "T16", "T32"};
    if (std::find(workloads.begin(), workloads.end(), opt.workload) == workloads.end())
        throw std::invalid_argument("unknown workload");
    bool overrides = false;
    for (int i = 3; i < argc; ++i) {
        const std::string_view flag = argv[i];
        if (flag == "--smoke") opt.smoke = true;
        else if (flag == "--smoke-size" || flag == "--smoke-samples") {
            if (++i == argc) throw std::invalid_argument("missing smoke override value");
            const auto value = positive_number(argv[i]);
            if (flag == "--smoke-size") opt.smoke_size = value;
            else opt.smoke_samples = value;
            overrides = true;
        } else {
            throw std::invalid_argument("unknown option");
        }
    }
    if (overrides && !opt.smoke)
        throw std::invalid_argument("size/sample overrides require --smoke");
    if (opt.smoke_size > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::invalid_argument("smoke size exceeds the ascending int key range");
    if (opt.smoke_samples % 2 == 0)
        throw std::invalid_argument("smoke sample count must be odd");
    return opt;
}
}

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        usage(argv[0]);
        return 0;
    }
    try {
        const auto opt = parse_options(argc, argv);
        if (opt.container == "vector") dispatch<true>(opt);
        else dispatch<false>(opt);
        return 0;
    } catch (const std::invalid_argument& error) {
        std::fprintf(stderr, "invalid benchmark arguments: %s\n", error.what());
        usage(argv[0]);
        return 2;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "sort benchmark failed: %s\n", error.what());
        return 1;
    }
}
