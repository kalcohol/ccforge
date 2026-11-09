// MIT License
// Copyright (c) 2026 Forge Project

#include "../../../backport/cpp26/hive/detail/bounded_sort.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
struct entry {
  const int* address;
  std::size_t source;
};

template<class T, class Compare>
void measure(const std::string& implementation, const std::string& workload,
             const std::vector<T>& input, Compare compare) {
  auto reference = input;
  std::sort(reference.begin(), reference.end(), compare);
  std::vector<double> samples;
  std::uint64_t checksum = 0;
  const int repeats = workload == "small" ? 2001 : 31;
  for (int iteration = -1; iteration < repeats; ++iteration) {
    auto values = input;
    const auto start = std::chrono::steady_clock::now();
    if (implementation == "bounded") {
#if defined(FORGE_HIVE_BENCH_SORT_THRESHOLD)
      std::size_t depth = 0;
      for (auto n = values.size(); n > 1; n >>= 1) {
        depth += 2;
      }
      std::__forge_hive_detail::__bounded_sort(values.data(), values.size(), compare,
          {FORGE_HIVE_BENCH_SORT_THRESHOLD, depth});
#else
      std::__forge_hive_detail::__bounded_sort(values.data(), values.size(), compare);
#endif
    } else {
      std::sort(values.begin(), values.end(), compare);
    }
    const auto finish = std::chrono::steady_clock::now();
    std::vector<unsigned char> seen;
    if constexpr (!std::is_same_v<T, int>) {
      seen.resize(values.size());
    }
    for (std::size_t i = 0; i < values.size(); ++i) {
      if constexpr (!std::is_same_v<T, int>) {
        const auto source = values[i].source;
        if (source >= input.size() || values[i].address != input[source].address || seen[source]++) {
          throw std::runtime_error("bounded sort cost probe permutation mismatch");
        }
      }
      if (compare(values[i], reference[i]) || compare(reference[i], values[i])) {
        throw std::runtime_error("bounded sort cost probe mismatch");
      }
      if constexpr (std::is_same_v<T, int>) {
        checksum += static_cast<std::uint32_t>(values[i]);
      } else {
        checksum += static_cast<std::uint32_t>(*values[i].address);
      }
    }
    if (iteration >= 0) {
      samples.push_back(std::chrono::duration<double, std::nano>(finish - start).count());
    }
  }
  for (std::size_t i = 0; i < samples.size(); ++i) {
    std::cerr << "sample_ns," << i << ',' << std::setprecision(17) << samples[i] << '\n';
  }
  std::sort(samples.begin(), samples.end());
  std::cout << std::setprecision(17)
            << "{\"implementation\":\"" << implementation
            << "\",\"workload\":\"" << workload << "\",\"n\":" << input.size()
            << ",\"samples\":" << repeats << ",\"median_ns\":"
            << samples[samples.size() / 2] << ",\"min_ns\":" << samples.front()
            << ",\"checksum\":" << checksum << "}\n";
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "usage: probe bounded|standard random|few|ordered|small|addresses\n";
    return 2;
  }
  const std::string implementation = argv[1];
  const std::string workload = argv[2];
  if ((implementation != "bounded" && implementation != "standard") ||
      (workload != "random" && workload != "few" && workload != "ordered" &&
       workload != "small" && workload != "addresses")) {
    return 2;
  }
  std::mt19937_64 random(1);
  const std::size_t count = workload == "small" ? 1000 : 1000000;
  std::vector<int> values(count);
  for (std::size_t i = 0; i < count; ++i) {
    values[i] = workload == "ordered" ? static_cast<int>(i)
        : static_cast<int>(random() % (workload == "few" ? 16 : 1000000000));
  }
  if (workload == "addresses") {
    std::vector<entry> entries;
    entries.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
      entries.push_back({&values[i], i});
    }
    measure(implementation, workload, entries,
            [](const entry& a, const entry& b) { return *a.address < *b.address; });
  } else {
    measure(implementation, workload, values, std::less<int>{});
  }
}
