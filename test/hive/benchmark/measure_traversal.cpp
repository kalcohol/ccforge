#include <hive>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <limits>
#include <random>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

namespace {
using clock_type = std::chrono::steady_clock;
constexpr std::uint64_t seed = 1;
static_assert(sizeof(int) == 4 && std::numeric_limits<int>::max() == 2147483647);

// External adapters need only replace the hive include and this alias.
template<class T>
using hive_container = std::hive<T>;

struct options {
    std::string_view container;
    std::string_view workload;
    bool smoke = false;
    std::size_t smoke_size = 257;
    std::size_t smoke_samples = 3;

    std::size_t size() const {
        if (smoke) return smoke_size;
        if (workload == "T7") return 200000;
        return workload == "T6" ? 1000 : 1000000;
    }
    std::size_t samples() const {
        return smoke ? smoke_samples : (workload == "T6" ? 2001 : 31);
    }
    bool churn() const { return workload == "T4" || workload == "T5"; }
    bool p1_workload() const { return workload != "T7" && workload != "T8"; }
};

struct trivial64 {
    int key;
    std::array<unsigned char, 60> payload;
    bool operator==(const trivial64&) const = default;
};
static_assert(sizeof(trivial64) == 64 && std::is_trivial_v<trivial64>);

enum class traversal { read, assign, increment, reverse };

int random_key(std::mt19937_64& rng) {
    // Leave headroom for the single increment in T3/T7, without signed overflow.
    return static_cast<int>(rng() & 0x3fffffffULL);
}

template<class T>
T random_value(std::mt19937_64& rng) {
    if constexpr (std::is_same_v<T, int>) {
        return random_key(rng);
    } else {
        T value{};
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

template<bool Vector, class C>
void add(C& container, const typename C::value_type& value) {
    if constexpr (Vector) container.push_back(value);
    else container.insert(value);
}

template<class T, bool Vector>
auto make_input(const options& opt, std::mt19937_64& rng) {
    using C = std::conditional_t<Vector, std::vector<T>, hive_container<T>>;
    C container;
    if constexpr (Vector) container.reserve(opt.size());
    for (std::size_t i = 0; i < opt.size(); ++i)
        add<Vector>(container, random_value<T>(rng));
    if (opt.churn()) {
        for (int round = 0; round < 3; ++round) {
            std::size_t removed = 0;
            if constexpr (Vector) {
                // Stable compaction implements the same erase decisions in O(n).
                // No hive is constructed in a vector process. Hive slot reuse may
                // produce a different traversal order and later churn input.
                std::size_t kept = 0;
                for (std::size_t i = 0; i < container.size(); ++i) {
                    if (rng() & 1U) ++removed;
                    else container[kept++] = container[i];
                }
                container.resize(kept);
            } else {
                for (auto it = container.begin(); it != container.end();) {
                    if (rng() & 1U) {
                        it = container.erase(it);
                        ++removed;
                    } else {
                        ++it;
                    }
                }
            }
            for (std::size_t i = 0; i < removed; ++i)
                add<Vector>(container, random_value<T>(rng));
        }
    }
    return container;
}

template<class T>
int& key(T& value) {
    if constexpr (std::is_same_v<T, int>) return value;
    else return value.key;
}

struct checksum {
    std::uint64_t value = 14695981039346656037ULL;

    void byte(unsigned char b) {
        value ^= b;
        value *= 1099511628211ULL;
    }
    template<class T>
    void append(const T& item) {
        std::uint32_t word;
        if constexpr (std::is_same_v<T, int>) word = static_cast<std::uint32_t>(item);
        else word = static_cast<std::uint32_t>(item.key);
        for (int i = 0; i < 4; ++i) {
            byte(static_cast<unsigned char>(word & 0xffU));
            word >>= 8;
        }
        if constexpr (!std::is_same_v<T, int>)
            for (unsigned char b : item.payload) byte(b);
    }
};

template<class C>
std::uint64_t payload_checksum(const C& values) {
    checksum hash;
    for (const auto& value : values) hash.append(value);
    return hash.value;
}

template<class C, class T>
std::uint64_t validate(const C& container, const std::vector<T>& expected) {
    if (container.size() != expected.size())
        throw std::runtime_error("traversal changed the size");
    checksum hash;
    std::size_t i = 0;
    for (const auto& value : container) {
        if (i == expected.size() || value != expected[i])
            throw std::runtime_error("traversal data or full-payload mismatch");
        hash.append(value);
        ++i;
    }
    if (i != expected.size())
        throw std::runtime_error("traversal visited the wrong number of elements");
    return hash.value;
}

template<traversal Mode, class C>
std::uint64_t traverse(C& container, const std::vector<int>& buffer) {
    if constexpr (Mode == traversal::read) {
        std::uint64_t sum = 0;
        for (const int value : std::as_const(container))
            sum += static_cast<std::uint64_t>(value);
        return sum;
    } else if constexpr (Mode == traversal::reverse) {
        std::uint64_t sum = 0;
        const auto& input = std::as_const(container);
        for (auto it = input.rbegin(); it != input.rend(); ++it)
            sum += static_cast<std::uint64_t>(*it);
        return sum;
    } else if constexpr (Mode == traversal::assign) {
        std::size_t i = 0;
        for (int& value : container) value = buffer[i++];
        return 0;
    } else {
        for (auto& value : container) key(value) += 1;
        return 0;
    }
}

template<class T, bool Vector, traversal Mode>
void execute(const options& opt, const char* type) {
    std::vector<double> samples(opt.samples());
    std::size_t capacity = 0;
    std::uint64_t input_hash = 0;
    std::uint64_t result_hash = 0;
    std::uint64_t result_sum = 0;
    using C = std::conditional_t<Vector, std::vector<T>, hive_container<T>>;
    for (std::size_t run = 0; run <= opt.samples(); ++run) {
        // Rebuild with seed 1 for warmup and each sample. All setup is untimed.
        std::mt19937_64 rng(seed);
        auto container = make_input<T, Vector>(opt, rng);
        if (container.size() != opt.size())
            throw std::runtime_error("input size mismatch");
        std::vector<T> expected(container.begin(), container.end());
        if (expected.size() != opt.size())
            throw std::runtime_error("input traversal count mismatch");
        const auto initial_hash = payload_checksum(expected);
        capacity = container.capacity();
        std::vector<int> buffer;
        if constexpr (Mode == traversal::assign) {
            buffer.resize(opt.size());
            for (auto& value : buffer) value = random_key(rng);
            std::copy(buffer.begin(), buffer.end(), expected.begin());
        } else if constexpr (Mode == traversal::increment) {
            for (auto& value : expected) key(value) += 1;
        }
        const auto expected_hash = payload_checksum(expected);
        std::uint64_t expected_sum = 0;
        if constexpr (Mode == traversal::read || Mode == traversal::reverse)
            for (const int value : expected)
                expected_sum += static_cast<std::uint64_t>(value);

        const auto begin = clock_type::now();
        std::atomic_signal_fence(std::memory_order_seq_cst);
        const auto sum = traverse<Mode>(container, buffer);
        std::atomic_signal_fence(std::memory_order_seq_cst);
        const auto end = clock_type::now();

        if (run != 0)
            samples[run - 1] = std::chrono::duration<double, std::nano>(end - begin).count();
        if (container.capacity() != capacity)
            throw std::runtime_error("traversal changed the capacity");
        const auto actual_hash = validate(container, expected);
        if (sum != expected_sum || actual_hash != expected_hash)
            throw std::runtime_error("traversal sum or checksum mismatch");
        if (run != 0 && (initial_hash != input_hash || actual_hash != result_hash))
            throw std::runtime_error("input or result differs between samples");
        input_hash = initial_hash;
        result_hash = actual_hash;
        result_sum = sum;
    }
    std::sort(samples.begin(), samples.end());
    // Order-statistic quartiles; each sample is exactly one traversal, even T6.
    const auto last = samples.size() - 1;
    const auto p75 = 3 * (last / 4) + (3 * (last % 4)) / 4;
    std::puts("container,workload,type,element_bytes,container_bytes,iterator_bytes,const_iterator_bytes,p0_workload,p1_workload,smoke,size,capacity,seed,churn_rounds,warmups,samples,timing_allocator,min_ns,p25_ns,median_ns,p75_ns,max_ns,input_checksum,payload_checksum,result_sum");
    std::printf("%.*s,%.*s,%s,%zu,%zu,%zu,%zu,true,%s,%s,%zu,%zu,%llu,%d,1,%zu,std_allocator,%.0f,%.0f,%.0f,%.0f,%.0f,%016llx,%016llx,%llu\n",
        static_cast<int>(opt.container.size()), opt.container.data(),
        static_cast<int>(opt.workload.size()), opt.workload.data(), type, sizeof(T),
        sizeof(C), sizeof(typename C::iterator), sizeof(typename C::const_iterator),
        opt.p1_workload() ? "true" : "false", opt.smoke ? "true" : "false",
        opt.size(), capacity, static_cast<unsigned long long>(seed), opt.churn() ? 3 : 0,
        opt.samples(), samples.front(), samples[last / 4],
        samples[samples.size() / 2], samples[p75], samples.back(),
        static_cast<unsigned long long>(input_hash),
        static_cast<unsigned long long>(result_hash), static_cast<unsigned long long>(result_sum));
}

template<bool Vector>
void dispatch(const options& opt) {
    if (opt.workload == "T7") execute<trivial64, Vector, traversal::increment>(opt, "trivial64");
    else if (opt.workload == "T8") execute<int, Vector, traversal::reverse>(opt, "int");
    else if (opt.workload == "T3") execute<int, Vector, traversal::increment>(opt, "int");
    else if (opt.workload == "T2" || opt.workload == "T5" || opt.workload == "T6")
        execute<int, Vector, traversal::assign>(opt, "int");
    else execute<int, Vector, traversal::read>(opt, "int");
}

void usage(const char* program) {
    std::fprintf(stderr,
        "usage: %s {hive|vector} {T1|T2|T3|T4|T5|T6|T7|T8}\n"
        "       [--smoke [--smoke-size N] [--smoke-samples ODD_N]]\n"
        "Production: one warmup, 31 samples (T6: 2001); taskbook sizes.\n"
        "Smoke: one warmup, size 257, 3 samples unless explicitly overridden.\n"
        "Each sample times one traversal; setup and full-data checks are untimed.\n"
        "Vector churn uses stable compaction, without constructing a hive.\n"
        "T7/T8 remain subject to P0; P1 covers T1-T6.\n"
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
    constexpr std::array workloads{"T1", "T2", "T3", "T4", "T5", "T6", "T7", "T8"};
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
    if (opt.smoke_samples == std::numeric_limits<std::size_t>::max() || opt.smoke_samples % 2 == 0)
        throw std::invalid_argument("smoke sample count must be odd and leave room for warmup");
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
        std::fprintf(stderr, "traversal benchmark failed: %s\n", error.what());
        return 1;
    }
}
