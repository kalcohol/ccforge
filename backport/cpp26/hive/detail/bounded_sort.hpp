// MIT License
//
// Copyright (c) 2026 Forge Project
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include <cstddef>
#include <cstring>
#include <type_traits>

namespace std::__forge_hive_detail {

#if defined(FORGE_HIVE_TEST_HOOKS)
struct __sort_test_options {
  size_t insertion_threshold;
  size_t depth_limit;
};

struct __sort_test_stats {
  size_t partitions = 0;
  size_t heap_fallbacks = 0;
  size_t insertions = 0;
};
#endif

template<class T>
void __sort_exchange(T* a, T* b) noexcept {
  static_assert(is_trivially_copyable_v<T>);
  if (a != b) {
    unsigned char saved[sizeof(T)];
    std::memcpy(saved, a, sizeof(T));
    std::memcpy(a, b, sizeof(T));
    std::memcpy(b, saved, sizeof(T));
  }
}

template<class T, class Compare>
void __sort_sift(T* data, size_t root, size_t count, Compare& comp) {
  // root < count/2 proves that 2*root+1 exists and cannot overflow.
  while (root < count / 2) {
    size_t child = root * 2 + 1;
    if (child + 1 < count && comp(data[child], data[child + 1])) {
      ++child;
    }
    if (!comp(data[root], data[child])) {
      return;
    }
    __sort_exchange(data + root, data + child);
    root = child;
  }
}

template<class T, class Compare>
void __sort_heap(T* data, size_t count, Compare& comp) {
  for (size_t root = count / 2; root != 0;) {
    __sort_sift(data, --root, count, comp);
  }
  for (size_t remaining = count; remaining > 1; --remaining) {
    __sort_exchange(data, data + remaining - 1);
    __sort_sift(data, 0, remaining - 1, comp);
  }
}

template<class T, class Compare>
void __sort_insert(T* data, size_t count, Compare& comp) {
  for (size_t next = 1; next < count; ++next) {
    size_t current = next;
    // Locate the insertion point before moving any bytes. The live array slot
    // remains the comparison source; no T construction or assignment is needed.
    while (current != 0 && comp(data[next], data[current - 1])) {
      --current;
    }
    if (current != next) {
      unsigned char saved[sizeof(T)];
      std::memcpy(saved, data + next, sizeof(T));
      std::memmove(data + current + 1, data + current, (next - current) * sizeof(T));
      std::memcpy(data + current, saved, sizeof(T));
    }
  }
}

template<class T, class Compare>
size_t __sort_pivot(T* data, size_t count, Compare& comp) {
  const size_t middle = count / 2;
  const size_t last = count - 1;
  if (comp(data[0], data[middle])) {
    if (comp(data[middle], data[last])) {
      return middle;
    }
    return comp(data[0], data[last]) ? last : 0;
  }
  if (comp(data[0], data[last])) {
    return 0;
  }
  return comp(data[middle], data[last]) ? last : middle;
}

template<class T, class Compare>
void __sort_partition_loop(T* data, size_t count, Compare& comp,
                           size_t threshold, size_t depth
#if defined(FORGE_HIVE_TEST_HOOKS)
                           , __sort_test_stats* stats
#endif
                           ) {
  // Independent textbook synthesis: Musser's depth-bounded introsort (1997),
  // three-way partitioning for duplicate keys (Bentley/McIlroy, 1993), and
  // Sedgewick's small-partition insertion sort. No library implementation is
  // used. Every bound and progress argument below is independent of comp.
  while (count > threshold) {
    if (depth == 0) {
#if defined(FORGE_HIVE_TEST_HOOKS)
      if (stats) {
        ++stats->heap_fallbacks;
      }
#endif
      __sort_heap(data, count, comp);
      return;
    }
    --depth;
#if defined(FORGE_HIVE_TEST_HOOKS)
    if (stats) {
      ++stats->partitions;
    }
#endif
    const size_t pivot = count - 1;
    __sort_exchange(data + __sort_pivot(data, count, comp), data + pivot);
    size_t scan_left = 0;
    size_t scan_right = pivot;
    size_t equal_left = 0;
    size_t equal_right = pivot;
    // Scan inward and exchange only misplaced pairs. Equivalent keys collect
    // at the ends, then rotate into the middle. Throughout, independently of
    // comp: equal_left <= scan_left <= scan_right <= equal_right <= pivot.
    // The pivot stays in its live array slot; no T constructor is required.
    while (scan_left < scan_right) {
      while (scan_left < scan_right && comp(data[scan_left], data[pivot])) {
        ++scan_left;
      }
      while (scan_left < scan_right && comp(data[pivot], data[scan_right - 1])) {
        --scan_right;
      }
      if (scan_left == scan_right) {
        break;
      }
      if (scan_right - scan_left == 1) {
        // Both bounded scans stopped on the same slot. Collect it once.
        __sort_exchange(data + equal_left, data + scan_left);
        ++equal_left;
        ++scan_left;
        break;
      }
      --scan_right;
      __sort_exchange(data + scan_left, data + scan_right);
      if (!comp(data[scan_left], data[pivot])) {
        __sort_exchange(data + equal_left, data + scan_left);
        ++equal_left;
      }
      if (!comp(data[pivot], data[scan_right])) {
        --equal_right;
        __sort_exchange(data + scan_right, data + equal_right);
      }
      ++scan_left;
    }
    const size_t less = scan_left - equal_left;
    for (size_t index = 0; index < equal_left; ++index) {
      __sort_exchange(data + index, data + scan_left - 1 - index);
    }
    const size_t right_equal_count = count - equal_right; // Includes the pivot.
    for (size_t index = 0; index < right_equal_count; ++index) {
      __sort_exchange(data + scan_left + index, data + equal_right + index);
    }
    const size_t right = scan_left + right_equal_count;
    const size_t right_count = count - right;
    // Even a contradictory comp cannot remove the pivot from the middle:
    // both remaining ranges are strictly smaller than count. Recursing only
    // on the shorter range bounds stack depth by log2(count).
    if (less < right_count) {
      __sort_partition_loop(data, less, comp, threshold, depth
#if defined(FORGE_HIVE_TEST_HOOKS)
                            , stats
#endif
                            );
      data += right;
      count = right_count;
    } else {
      __sort_partition_loop(data + right, right_count, comp, threshold, depth
#if defined(FORGE_HIVE_TEST_HOOKS)
                            , stats
#endif
                            );
      count = less;
    }
  }
#if defined(FORGE_HIVE_TEST_HOOKS)
  if (stats && count > 1) {
    ++stats->insertions;
  }
#endif
  __sort_insert(data, count, comp);
}

template<class T, class Compare>
bool __sort_with_limits(T* data, size_t count, Compare& comp,
                        size_t threshold, size_t depth
#if defined(FORGE_HIVE_TEST_HOOKS)
                        , __sort_test_stats* stats
#endif
                        ) {
  static_assert(is_trivially_copyable_v<T>);
  if (count < 2) {
    return false;
  }
  size_t next = 1;
  while (next < count && !comp(data[next], data[next - 1])) {
    ++next;
  }
  if (next == count) {
    return false;
  }
  next = 1;
  while (next < count && !comp(data[next - 1], data[next])) {
    ++next;
  }
  if (next == count) {
    for (size_t left = 0; left < count / 2; ++left) {
      __sort_exchange(data + left, data + count - 1 - left);
    }
    return true;
  }
  // Even test overrides retain the minimum needed by the three-point pivot.
  __sort_partition_loop(data, count, comp, threshold < 2 ? 2 : threshold, depth
#if defined(FORGE_HIVE_TEST_HOOKS)
                        , stats
#endif
                        );
  return true;
}

template<class T, class Compare>
bool __bounded_sort(T* data, size_t count, Compare& comp) {
  size_t depth = 0;
  for (size_t remaining = count; remaining > 1; remaining >>= 1) {
    depth += 2;
  }
  return __sort_with_limits(data, count, comp, 24, depth
#if defined(FORGE_HIVE_TEST_HOOKS)
                            , nullptr
#endif
                            );
}

#if defined(FORGE_HIVE_TEST_HOOKS)
template<class T, class Compare>
bool __bounded_sort(T* data, size_t count, Compare& comp,
                    __sort_test_options options, __sort_test_stats* stats = nullptr) {
  return __sort_with_limits(data, count, comp, options.insertion_threshold,
                            options.depth_limit, stats);
}
#endif

}  // namespace std::__forge_hive_detail
