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

// No shared <version> wrapper is used. A native macro or the declaration-level
// CMake signal suppresses injection, including for partial native libraries.
// Force is diagnostic-only and does not make mixed native/backport use safe.
#if !defined(FORGE_BACKPORT_HIVE_HPP_INCLUDED) && \
    (defined(FORGE_FORCE_HIVE_BACKPORT) || \
     (!defined(FORGE_HAS_NATIVE_HIVE) && !defined(__cpp_lib_hive)))

#define FORGE_BACKPORT_HIVE_HPP_INCLUDED 1

#include <algorithm>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <memory_resource>
#include <ranges>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace std {

struct hive_limits {
  size_t min;
  size_t max;

  constexpr hive_limits(size_t minimum, size_t maximum) noexcept
      : min(minimum), max(maximum) {}
};

namespace __forge_hive_detail {

// Substitute the full constant expression outside hive's class instantiation.
// Some compilers otherwise reject a valid nontrivial constexpr allocator.
template<class A, class = void>
struct constexpr_allocator_bound {};

template<class A>
struct constexpr_allocator_bound<A, void_t<integral_constant<
    typename allocator_traits<A>::size_type,
    allocator_traits<A>::max_size(A{})>>>
    : integral_constant<typename allocator_traits<A>::size_type,
                        allocator_traits<A>::max_size(A{})> {};

// The standard's container-compatible-range is exposition-only.  Keep a Forge
// exposition-only equivalent so this header does not depend on an implementation
// having adopted the C++23 spelling of that concept.
template<class R, class T>
concept compatible_range =
    ranges::input_range<R> &&
    convertible_to<ranges::range_reference_t<R>, T>;

template<class A>
concept allocator = requires {
  typename A::value_type;
} && requires(A& a) {
  a.allocate(size_t{});
};

// Sequence-container iterator overloads use Cpp17InputIterator qualification,
// not the stronger C++20 indirectly-readable/common-reference requirements.
// In particular, a legacy proxy reference may be EmplaceConstructible into T
// without modeling std::input_iterator.
template<class I>
concept input_iterator = requires(I& i, const I& ci) {
  typename iterator_traits<I>::value_type;
  typename iterator_traits<I>::difference_type;
  typename iterator_traits<I>::reference;
  typename iterator_traits<I>::iterator_category;
  typename iterator_traits<I>::pointer;
  { *ci };
  { ++i } -> same_as<I&>;
  { i++ };
  { ci == ci } -> convertible_to<bool>;
  { ci != ci } -> convertible_to<bool>;
} && derived_from<
    typename iterator_traits<I>::iterator_category, input_iterator_tag>;

} // namespace __forge_hive_detail

template<class T, class Allocator = allocator<T>>
class hive {
 public:
  using value_type = T;
  using allocator_type = Allocator;
  using pointer = typename allocator_traits<Allocator>::pointer;
  using const_pointer = typename allocator_traits<Allocator>::const_pointer;
  using reference = value_type&;
  using const_reference = const value_type&;
  using size_type = size_t;
  using difference_type = ptrdiff_t;

 private:
  using allocator_traits_type = allocator_traits<Allocator>;

  static_assert(is_same_v<typename allocator_type::value_type, value_type>,
                "hive::allocator_type::value_type must be hive::value_type");

  struct control_type;
  struct block_type;
  using slot_index_type = uint16_t;

  struct free_run_links {
    slot_index_type previous;
    slot_index_type next;
  };

  // Address-order traversal uses jump counts, following the design ideas in
  // M. Bentley, "The Advanced 'Jump-Counting' Skipfield Pattern" (2017),
  // doi:10.1007/s40869-017-0038-3, and P0447. This implementation is independent.
  // Slots have no metadata. A dead run's first slot holds links as bytes only.
  struct slot_type {
    alignas(T) unsigned char storage[
        std::max(sizeof(T), sizeof(free_run_links))];
  };

  using block_allocator =
      typename allocator_traits_type::template rebind_alloc<block_type>;
  using block_traits = allocator_traits<block_allocator>;
  using block_allocation_pointer = typename block_traits::pointer;
  using slot_allocator =
      typename allocator_traits_type::template rebind_alloc<slot_type>;
  using slot_traits = allocator_traits<slot_allocator>;
  using slot_allocation_pointer = typename slot_traits::pointer;
  using skip_allocator =
      typename allocator_traits_type::template rebind_alloc<slot_index_type>;
  using skip_traits = allocator_traits<skip_allocator>;
  using skip_allocation_pointer = typename skip_traits::pointer;
  using control_allocator =
      typename allocator_traits_type::template rebind_alloc<control_type>;
  using control_traits = allocator_traits<control_allocator>;
  using control_allocation_pointer = typename control_traits::pointer;

  static constexpr size_type npos = numeric_limits<size_type>::max();
  static constexpr slot_index_type slot_npos =
      numeric_limits<slot_index_type>::max();

  static constexpr bool sort_uses_value_buffer =
      sizeof(T) <= 16 && is_trivially_copyable_v<T> &&
      is_trivially_copy_constructible_v<T> && is_trivially_copy_assignable_v<T> &&
      is_move_constructible_v<T> && is_move_assignable_v<T>;

  // Only real indices or npos cross this boundary; counts remain wide.
  // The block hard limit keeps every real index strictly below slot_npos.
  static constexpr slot_index_type to_slot_index(size_type index) noexcept {
    return index == npos ? slot_npos : static_cast<slot_index_type>(index);
  }

  static constexpr size_type to_wide_index(slot_index_type index) noexcept {
    return index == slot_npos ? npos : static_cast<size_type>(index);
  }

  static constexpr size_type structural_max_size() noexcept {
    constexpr size_type by_slots =
        numeric_limits<size_type>::max() /
        (sizeof(slot_type) + sizeof(slot_index_type));
    constexpr size_type by_difference =
        static_cast<size_type>(numeric_limits<difference_type>::max());
    return std::min({by_slots, by_difference, npos - 1});
  }

  static constexpr size_type static_max_size() noexcept {
    constexpr size_type by_structure = structural_max_size();
    if constexpr (std::is_empty_v<allocator_type> &&
                  requires {
                    typename __forge_hive_detail::constexpr_allocator_bound<
                        allocator_type>::type;
                  }) {
      // The full query, including destruction, must constant evaluate. Keep
      // this existing type-level clamp separate from the per-block index cap.
      constexpr auto allocator_max =
          __forge_hive_detail::constexpr_allocator_bound<allocator_type>::value;
      using common_type = common_type_t<decltype(allocator_max), size_type>;
      return static_cast<size_type>(
          std::min<common_type>(by_structure, allocator_max));
    } else {
      return by_structure;
    }
  }

  struct block_type {
    block_type(control_type* control,
               block_allocation_pointer block_allocation,
               slot_allocation_pointer slot_allocation,
               skip_allocation_pointer skip_allocation,
               size_type block_capacity)
        : control(control),
          block_allocation(block_allocation),
          slot_allocation(slot_allocation),
          skip_allocation(skip_allocation),
          slots(to_address(slot_allocation)),
          skip(to_address(skip_allocation)),
          capacity(block_capacity) {}

    control_type* control;
    block_allocation_pointer block_allocation;
    slot_allocation_pointer slot_allocation;
    skip_allocation_pointer skip_allocation;
    slot_type* slots;
    slot_index_type* skip;
    size_type capacity;
    size_type live_count = 0;
    // Only [0, high_water) is readable in skip; zero denotes a live slot.
    // The last initialized slot is live. Dead runs have their length at both
    // ends, nonzero interiors, and one free-list node at the run's first slot.
    size_type high_water = 0;
    size_type free_head = npos;
    size_type order = 0;
    block_type* active_prev = nullptr;
    block_type* active_next = nullptr;
    block_type* reserved_next = nullptr;
    block_type* available_prev = nullptr;
    block_type* available_next = nullptr;
    bool active = false;
    bool in_available = false;
  };

  struct control_type {
    control_type(hive* container,
                 control_allocation_pointer control_allocation)
        : owner(container), control_allocation(control_allocation) {}

    hive* owner;
    control_allocation_pointer control_allocation;
    block_type* active_head = nullptr;
    block_type* active_tail = nullptr;
    block_type* reserved_head = nullptr;
    block_type* available_head = nullptr;
    size_type size = 0;
    size_type capacity = 0;
    size_type next_block_order = 0;
  };

  template<bool IsConst>
  class hive_iterator {
   public:
    using iterator_category = bidirectional_iterator_tag;
    using iterator_concept = bidirectional_iterator_tag;
    using value_type = T;
    using difference_type = ptrdiff_t;
    using reference = conditional_t<IsConst, const T&, T&>;
    using pointer = conditional_t<IsConst, const T*, T*>;

    constexpr hive_iterator() noexcept = default;
    constexpr hive_iterator(const hive_iterator&) noexcept = default;
    constexpr hive_iterator& operator=(const hive_iterator&) noexcept = default;

    // Conversion is deliberately one-way: iterator -> const_iterator.
    template<bool OtherConst>
      requires(IsConst && !OtherConst)
    constexpr hive_iterator(const hive_iterator<OtherConst>& other) noexcept
        : block_(other.block_), index_(other.index_), owner_(other.owner_),
          epoch_(other.epoch_) {}

    constexpr reference operator*() const noexcept {
      return *std::launder(reinterpret_cast<pointer>(
          block_->slots[index_].storage));
    }

    constexpr pointer operator->() const noexcept {
      return std::launder(reinterpret_cast<pointer>(
          block_->slots[index_].storage));
    }

    constexpr hive_iterator& operator++() noexcept {
      const size_type next = index_ + 1;
      if (next < block_->high_water) {
        index_ = next + block_->skip[next];
      } else {
        control_type* const control = block_->control;
        block_ = block_->active_next;
        if (block_) {
          index_ = block_->skip[0];
        } else {
          // End iterators carry an owner, never a pointer which has to be
          // dereferenced after POCCA replaces a control state.
          owner_ = control->owner;
          index_ = npos;
          epoch_ = owner_->end_epoch_;
        }
      }
      return *this;
    }

    constexpr hive_iterator operator++(int) noexcept {
      hive_iterator result(*this);
      ++*this;
      return result;
    }

    constexpr hive_iterator& operator--() noexcept {
      if (!block_) {
        // Decrementing an empty/singular end is outside the iterator contract.
        // Keeping this as a no-op is only a diagnostic convenience.
        if (!owner_ || !owner_->control_ ||
            !owner_->control_->active_tail) {
          return *this;
        }
        block_ = owner_->control_->active_tail;
        index_ = block_->high_water - 1;
        owner_ = nullptr;
        epoch_ = 0;
      } else {
        if (index_ != 0) {
          const size_type previous = index_ - 1;
          const size_type skipped = block_->skip[previous];
          if (skipped <= previous) {
            index_ = previous - skipped;
            return *this;
          }
        }
        block_ = block_->active_prev;
        index_ = block_->high_water - 1;
      }
      return *this;
    }

    constexpr hive_iterator operator--(int) noexcept {
      hive_iterator result(*this);
      --*this;
      return result;
    }

    template<bool OtherConst>
    constexpr bool operator==(
        const hive_iterator<OtherConst>& other) const noexcept {
      if (block_ || other.block_) {
        return block_ && other.block_ && block_ == other.block_ &&
               index_ == other.index_;
      }
      return owner_ == other.owner_ && epoch_ == other.epoch_;
    }

    template<bool OtherConst>
    constexpr strong_ordering operator<=>(
        const hive_iterator<OtherConst>& other) const noexcept {
      if (block_ && other.block_) {
        if (block_ == other.block_) {
          return index_ <=> other.index_;
        }
        control_type* const left_control = block_->control;
        control_type* const right_control = other.block_->control;
        if (left_control == right_control) {
          return block_->order <=> other.block_->order;
        }
        const strong_ordering owners =
            identity_order(left_control->owner, right_control->owner);
        if (owners != strong_ordering::equivalent) {
          return owners;
        }
        return identity_order(block_, other.block_);
      }

      if (block_ || other.block_) {
        const hive* const left_owner = block_ ? block_->control->owner : owner_;
        const hive* const right_owner =
            other.block_ ? other.block_->control->owner : other.owner_;
        if (left_owner == right_owner) {
          // A non-end element precedes end in its own comparison domain.
          return block_ ? strong_ordering::less : strong_ordering::greater;
        }
        return identity_order(left_owner, right_owner);
      }

      if (owner_ == other.owner_) {
        return epoch_ <=> other.epoch_;
      }
      return identity_order(owner_, other.owner_);
    }

   private:
    template<bool>
    friend class hive_iterator;
    friend class hive;

    constexpr hive_iterator(block_type* block, size_type index) noexcept
        : block_(block), index_(index) {}

    constexpr explicit hive_iterator(const hive* owner) noexcept
        : index_(npos), owner_(owner), epoch_(owner->end_epoch_) {}

    static constexpr strong_ordering identity_order(
        const void* left, const void* right) noexcept {
      if (left == right) {
        return strong_ordering::equivalent;
      }
      return less<const void*>{}(left, right)
                 ? strong_ordering::less
                 : strong_ordering::greater;
    }

    block_type* block_ = nullptr;
    size_type index_ = npos;
    const hive* owner_ = nullptr;
    size_type epoch_ = 0;
  };

 public:
  using iterator = hive_iterator<false>;
  using const_iterator = hive_iterator<true>;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

 private:
  using pocca = typename allocator_traits_type::
      propagate_on_container_copy_assignment;
  using pocma = typename allocator_traits_type::
      propagate_on_container_move_assignment;
  using pocs = typename allocator_traits_type::propagate_on_container_swap;
  using always_equal = typename allocator_traits_type::is_always_equal;

  static constexpr T* raw_element(slot_type& slot) noexcept {
    return reinterpret_cast<T*>(slot.storage);
  }

  static constexpr const T* raw_element(const slot_type& slot) noexcept {
    return reinterpret_cast<const T*>(slot.storage);
  }

  static constexpr T* element(slot_type& slot) noexcept {
    return std::launder(reinterpret_cast<T*>(slot.storage));
  }

  static constexpr const T* element(const slot_type& slot) noexcept {
    return std::launder(reinterpret_cast<const T*>(slot.storage));
  }

  control_type* allocate_control() {
    control_allocator allocator_for_control(alloc_);
    const control_allocation_pointer allocation =
        control_traits::allocate(allocator_for_control, 1);
    control_type* const control = to_address(allocation);
    try {
      control_traits::construct(allocator_for_control, control, this,
                                allocation);
    } catch (...) {
      control_traits::deallocate(allocator_for_control, allocation, 1);
      throw;
    }
    return control;
  }

  void deallocate_control(control_type* control) noexcept {
    control_allocator allocator_for_control(alloc_);
    const control_allocation_pointer allocation =
        control->control_allocation;
    control_traits::destroy(allocator_for_control, control);
    control_traits::deallocate(allocator_for_control, allocation, 1);
  }

  block_type* allocate_block(control_type* control, size_type capacity) {
    block_allocator allocator_for_blocks(alloc_);
    slot_allocator allocator_for_slots(alloc_);
    skip_allocator allocator_for_skip(alloc_);
    const block_allocation_pointer block_allocation =
        block_traits::allocate(allocator_for_blocks, 1);
    slot_allocation_pointer slot_allocation;
    try {
      slot_allocation =
          slot_traits::allocate(allocator_for_slots, capacity);
    } catch (...) {
      block_traits::deallocate(allocator_for_blocks, block_allocation, 1);
      throw;
    }

    skip_allocation_pointer skip_allocation;
    try {
      skip_allocation = skip_traits::allocate(allocator_for_skip, capacity);
    } catch (...) {
      slot_traits::deallocate(allocator_for_slots, slot_allocation, capacity);
      block_traits::deallocate(allocator_for_blocks, block_allocation, 1);
      throw;
    }

    block_type* const block = to_address(block_allocation);
    try {
      block_traits::construct(allocator_for_blocks, block, control,
                              block_allocation, slot_allocation,
                              skip_allocation, capacity);
    } catch (...) {
      skip_traits::deallocate(allocator_for_skip, skip_allocation, capacity);
      slot_traits::deallocate(allocator_for_slots, slot_allocation, capacity);
      block_traits::deallocate(allocator_for_blocks, block_allocation, 1);
      throw;
    }
    return block;
  }

  void deallocate_block(block_type* block) noexcept {
    block_allocator allocator_for_blocks(alloc_);
    slot_allocator allocator_for_slots(alloc_);
    skip_allocator allocator_for_skip(alloc_);
    const block_allocation_pointer block_allocation =
        block->block_allocation;
    const slot_allocation_pointer slot_allocation = block->slot_allocation;
    const skip_allocation_pointer skip_allocation = block->skip_allocation;
    const size_type capacity = block->capacity;
    block_traits::destroy(allocator_for_blocks, block);
    skip_traits::deallocate(allocator_for_skip, skip_allocation, capacity);
    slot_traits::deallocate(allocator_for_slots, slot_allocation, capacity);
    block_traits::deallocate(allocator_for_blocks, block_allocation, 1);
  }

  void destroy_live_elements(block_type* block) noexcept {
    size_type index = block->high_water == 0 ? 0 : block->skip[0];
    while (index < block->high_water) {
      allocator_traits_type::destroy(alloc_, element(block->slots[index]));
      ++index;
      if (index < block->high_water) {
        index += block->skip[index];
      }
    }
  }

  static void reset_vacant_block(block_type* block) noexcept {
    block->live_count = 0;
    block->high_water = 0;
    block->free_head = npos;
  }

  static void available_add(control_type* control,
                            block_type* block) noexcept {
    block->in_available = true;
    block->available_prev = nullptr;
    block->available_next = control->available_head;
    if (control->available_head) {
      control->available_head->available_prev = block;
    }
    control->available_head = block;
  }

  static void available_remove(control_type* control,
                               block_type* block) noexcept {
    if (!block->in_available) {
      return;
    }
    if (block->available_prev) {
      block->available_prev->available_next = block->available_next;
    } else {
      control->available_head = block->available_next;
    }
    if (block->available_next) {
      block->available_next->available_prev = block->available_prev;
    }
    block->available_prev = nullptr;
    block->available_next = nullptr;
    block->in_available = false;
  }

  static void active_append(control_type* control,
                            block_type* block) noexcept {
    block->active = true;
    block->active_prev = control->active_tail;
    block->active_next = nullptr;
    block->order = control->next_block_order++;
    if (control->active_tail) {
      control->active_tail->active_next = block;
    } else {
      control->active_head = block;
    }
    control->active_tail = block;
  }

  static void active_remove(control_type* control,
                            block_type* block) noexcept {
    if (block->active_prev) {
      block->active_prev->active_next = block->active_next;
    } else {
      control->active_head = block->active_next;
    }
    if (block->active_next) {
      block->active_next->active_prev = block->active_prev;
    } else {
      control->active_tail = block->active_prev;
    }
    block->active_prev = nullptr;
    block->active_next = nullptr;
    block->active = false;
  }

  static void link_reserved(control_type* control,
                            block_type* block) noexcept {
    block->active = false;
    block->reserved_next = control->reserved_head;
    control->reserved_head = block;
    control->capacity += block->capacity;
  }

  static free_run_links read_free_links(const block_type* block,
                                       size_type index) noexcept {
    free_run_links links;
    std::memcpy(&links, block->slots[index].storage, sizeof(links));
    return links;
  }

  static void write_free_links(block_type* block, size_type index,
                               free_run_links links) noexcept {
    std::memcpy(block->slots[index].storage, &links, sizeof(links));
  }

  static void unlink_free_run(block_type* block,
                              free_run_links links) noexcept {
    if (links.previous == slot_npos) {
      block->free_head = to_wide_index(links.next);
    } else {
      free_run_links previous = read_free_links(block, links.previous);
      previous.next = links.next;
      write_free_links(block, links.previous, previous);
    }
    if (links.next != slot_npos) {
      free_run_links next = read_free_links(block, links.next);
      next.previous = links.previous;
      write_free_links(block, links.next, next);
    }
  }

  static void move_free_run(block_type* block, size_type index,
                            free_run_links links) noexcept {
    write_free_links(block, index, links);
    if (links.previous == slot_npos) {
      block->free_head = index;
    } else {
      free_run_links previous = read_free_links(block, links.previous);
      previous.next = to_slot_index(index);
      write_free_links(block, links.previous, previous);
    }
    if (links.next != slot_npos) {
      free_run_links next = read_free_links(block, links.next);
      next.previous = to_slot_index(index);
      write_free_links(block, links.next, next);
    }
  }

  static void push_free_run(block_type* block, size_type index) noexcept {
    move_free_run(block, index,
                  {slot_npos, to_slot_index(block->free_head)});
  }

  void touch_end() noexcept { ++end_epoch_; }

  void clear_without_end_invalidation() noexcept {
    control_type* const control = control_;
    if (!control) {
      return;
    }

    block_type* block = control->active_head;
    while (block) {
      block_type* const next = block->active_next;
      destroy_live_elements(block);
      reset_vacant_block(block);
      block->active = false;
      block->active_prev = nullptr;
      block->active_next = nullptr;
      block->available_prev = nullptr;
      block->available_next = nullptr;
      block->in_available = false;
      block->reserved_next = control->reserved_head;
      control->reserved_head = block;
      block = next;
    }
    control->active_head = nullptr;
    control->active_tail = nullptr;
    control->available_head = nullptr;
    control->size = 0;
  }

  void release_storage() noexcept {
    control_type* const control = control_;
    if (!control) {
      return;
    }

    block_type* block = control->active_head;
    while (block) {
      block_type* const next = block->active_next;
      destroy_live_elements(block);
      deallocate_block(block);
      block = next;
    }
    block = control->reserved_head;
    while (block) {
      block_type* const next = block->reserved_next;
      deallocate_block(block);
      block = next;
    }
    deallocate_control(control);
    control_ = nullptr;
  }

  void steal_from(hive& other) noexcept {
    control_ = other.control_;
    other.control_ = nullptr;
    if (control_) {
      control_->owner = this;
    }
    limits_ = other.limits_;
  }

  static constexpr void validate_limits(hive_limits limits) {
    // Invalid user limits have erroneous behavior in the fixed draft.  Forge
    // chooses a length_error with no allocation as its implementation-defined
    // effect, rather than relying on undefined behavior.
    if (!is_within_hard_limits(limits)) {
      throw length_error("std::hive: invalid block capacity limits");
    }
  }

  static constexpr bool within(hive_limits limits,
                               size_type capacity) noexcept {
    return limits.min <= capacity && capacity <= limits.max;
  }

  size_type allocation_block_max() const {
    slot_allocator allocator_for_slots(alloc_);
    skip_allocator allocator_for_skip(alloc_);
    const auto slot_max = slot_traits::max_size(allocator_for_slots);
    const auto skip_max = skip_traits::max_size(allocator_for_skip);
    using common_type =
        common_type_t<decltype(slot_max), decltype(skip_max), size_type>;
    return static_cast<size_type>(
        std::min<common_type>({limits_.max, slot_npos, slot_max, skip_max}));
  }

  struct capacity_plan {
    size_type blocks;
    size_type added;
    size_type block_max;
  };

  bool plan_capacity(size_type current, size_type requested,
                     capacity_plan& plan) const {
    const size_type maximum = max_size();
    const size_type block_max = allocation_block_max();
    if (requested > maximum || current > maximum || limits_.min == 0 ||
        block_max < limits_.min) {
      return false;
    }

    // Callers request growth. O(1) planning: k = ceil(deficit / block_max),
    // added = max(deficit, k * min). Check headroom before multiplying.
    const size_type missing = requested - current;
    const size_type blocks =
        missing / block_max + (missing % block_max != 0 ? 1 : 0);
    if (blocks > (maximum - current) / limits_.min) {
      return false;
    }
    plan = {blocks, std::max(missing, blocks * limits_.min), block_max};
    return true;
  }

  void reserve_blocks(capacity_plan plan) {
    while (plan.blocks != 0) {
      // Leave a legal minimum for every tail block rather than greedily
      // consuming the headroom (e.g. min 8, max 10, added 16 -> 8 + 8).
      const size_type block_capacity = std::min(
          plan.block_max, plan.added - (plan.blocks - 1) * limits_.min);
      control_type* control = control_;
      const bool new_control = control == nullptr;
      if (new_control) {
        control = allocate_control();
      }

      block_type* block;
      try {
        block = allocate_block(control, block_capacity);
      } catch (...) {
        if (new_control) {
          deallocate_control(control);
        }
        throw;
      }

      if (new_control) {
        control_ = control;
      }
      link_reserved(control, block);
      plan.added -= block_capacity;
      --plan.blocks;
    }
  }

  template<class InputIterator>
  void append_input(InputIterator first, InputIterator last) {
    while (first != last) {
      emplace(*first);
      ++first;
    }
  }

  template<class R>
  void append_range(R&& range) {
    auto first = ranges::begin(range);
    const auto last = ranges::end(range);
    while (first != last) {
      emplace(*first);
      ++first;
    }
  }

  void append_default(size_type count) {
    while (count != 0) {
      emplace();
      --count;
    }
  }

  void append_copies(size_type count, const T& value) {
    while (count != 0) {
      emplace(value);
      --count;
    }
  }

  void append_moves(hive& source) {
    auto current = source.begin();
    const auto last = source.end();
    while (current != last) {
      emplace(std::move(*current));
      ++current;
    }
  }

  template<class InputIterator>
  void assign_input(InputIterator first, InputIterator last) {
    const size_type saved_epoch = end_epoch_;
    clear_without_end_invalidation();
    try {
      append_input(first, last);
    } catch (...) {
      end_epoch_ = saved_epoch;
      throw;
    }
    end_epoch_ = saved_epoch;
  }

 public:
  // Only the four empty-container constructors have the constexpr contract.
  constexpr hive() noexcept(noexcept(Allocator())) : hive(Allocator()) {}

  constexpr explicit hive(const Allocator& allocator) noexcept
      : alloc_(allocator) {}

  constexpr explicit hive(hive_limits block_limits)
      : hive(block_limits, Allocator()) {}

  constexpr hive(hive_limits block_limits, const Allocator& allocator)
      : alloc_(allocator), limits_(block_limits) {
    validate_limits(block_limits);
  }

  explicit hive(size_type n, const Allocator& allocator = Allocator())
      : hive(allocator) {
    append_default(n);
  }

  hive(size_type n, hive_limits block_limits,
       const Allocator& allocator = Allocator())
      : hive(block_limits, allocator) {
    append_default(n);
  }

  hive(size_type n, const T& value,
       const Allocator& allocator = Allocator())
      : hive(allocator) {
    append_copies(n, value);
  }

  hive(size_type n, const T& value, hive_limits block_limits,
       const Allocator& allocator = Allocator())
      : hive(block_limits, allocator) {
    append_copies(n, value);
  }

  template<__forge_hive_detail::input_iterator InputIterator>
  hive(InputIterator first, InputIterator last,
       const Allocator& allocator = Allocator())
      : hive(allocator) {
    append_input(first, last);
  }

  template<__forge_hive_detail::input_iterator InputIterator>
  hive(InputIterator first, InputIterator last, hive_limits block_limits,
       const Allocator& allocator = Allocator())
      : hive(block_limits, allocator) {
    append_input(first, last);
  }

  template<__forge_hive_detail::compatible_range<T> R>
  hive(from_range_t, R&& range, const Allocator& allocator = Allocator())
      : hive(allocator) {
    append_range(range);
  }

  template<__forge_hive_detail::compatible_range<T> R>
  hive(from_range_t, R&& range, hive_limits block_limits,
       const Allocator& allocator = Allocator())
      : hive(block_limits, allocator) {
    append_range(range);
  }

  hive(const hive& other)
      : hive(other.limits_,
             allocator_traits_type::select_on_container_copy_construction(
                 other.alloc_)) {
    append_input(other.begin(), other.end());
  }

  hive(hive&& other) noexcept : alloc_(std::move(other.alloc_)) {
    steal_from(other);
    touch_end();
    other.touch_end();
  }

  hive(const hive& other, const type_identity_t<Allocator>& allocator)
      : hive(other.limits_, allocator) {
    append_input(other.begin(), other.end());
  }

  hive(hive&& other, const type_identity_t<Allocator>& allocator)
      : hive(allocator) {
    if constexpr (always_equal::value) {
      steal_from(other);
      touch_end();
      other.touch_end();
    } else {
      if (alloc_ == other.alloc_) {
        steal_from(other);
        touch_end();
        other.touch_end();
      } else {
        // In the unequal-allocator case the fixed draft does not transfer the
        // source current-limits; this target keeps its default limits.
        append_moves(other);
        other.clear();
      }
    }
  }

  hive(initializer_list<T> values,
       const Allocator& allocator = Allocator())
      : hive(values.begin(), values.end(), allocator) {}

  hive(initializer_list<T> values, hive_limits block_limits,
       const Allocator& allocator = Allocator())
      : hive(values.begin(), values.end(), block_limits, allocator) {}

  ~hive() { release_storage(); }

  hive& operator=(const hive& other) {
    if (this == &other) {
      return *this;
    }

    const size_type saved_epoch = end_epoch_;
    if constexpr (pocca::value) {
      // All storage allocated with the old state is released before POCCA
      // replaces it.  The owner+epoch end representation survives this reset.
      release_storage();
      alloc_ = other.alloc_;
    } else {
      clear_without_end_invalidation();
    }

    try {
      append_input(other.begin(), other.end());
    } catch (...) {
      end_epoch_ = saved_epoch;
      throw;
    }
    end_epoch_ = saved_epoch;
    return *this;
  }

  hive& operator=(hive&& other) noexcept(pocma::value || always_equal::value) {
    if (this == &other) {
      return *this;
    }

    if constexpr (pocma::value || always_equal::value) {
      // This branch is discarded for the other allocator configuration, so a
      // steal-capable hive never instantiates an unnecessary T move operation.
      release_storage();
      if constexpr (pocma::value) {
        alloc_ = std::move(other.alloc_);
      }
      steal_from(other);
      touch_end();
      other.touch_end();
      return *this;
    } else {
      if (alloc_ == other.alloc_) {
        release_storage();
        steal_from(other);
        touch_end();
        other.touch_end();
        return *this;
      }

      const size_type saved_epoch = end_epoch_;
      clear_without_end_invalidation();
      try {
        append_moves(other);
      } catch (...) {
        end_epoch_ = saved_epoch;
        throw;
      }
      other.clear();
      // The non-stealing path does not transfer source current-limits.
      end_epoch_ = saved_epoch;
      return *this;
    }
  }

  hive& operator=(initializer_list<T> values) {
    assign(values.begin(), values.end());
    return *this;
  }

  template<__forge_hive_detail::input_iterator InputIterator>
  void assign(InputIterator first, InputIterator last) {
    assign_input(first, last);
  }

  template<__forge_hive_detail::compatible_range<T> R>
  void assign_range(R&& range) {
    static_assert(assignable_from<T&, ranges::range_reference_t<R>>,
                  "hive::assign_range requires assignable_from<T&, reference>");
    const size_type saved_epoch = end_epoch_;
    clear_without_end_invalidation();
    try {
      append_range(range);
    } catch (...) {
      end_epoch_ = saved_epoch;
      throw;
    }
    end_epoch_ = saved_epoch;
  }

  void assign(size_type count, const T& value) {
    const size_type saved_epoch = end_epoch_;
    clear_without_end_invalidation();
    try {
      append_copies(count, value);
    } catch (...) {
      end_epoch_ = saved_epoch;
      throw;
    }
    end_epoch_ = saved_epoch;
  }

  void assign(initializer_list<T> values) {
    assign(values.begin(), values.end());
  }

  allocator_type get_allocator() const noexcept { return alloc_; }

  iterator begin() noexcept {
    if (control_ && control_->active_head) {
      block_type* const block = control_->active_head;
      return iterator(block, block->skip[0]);
    }
    return end();
  }

  const_iterator begin() const noexcept {
    if (control_ && control_->active_head) {
      block_type* const block = control_->active_head;
      return const_iterator(block, block->skip[0]);
    }
    return end();
  }

  iterator end() noexcept { return iterator(this); }
  const_iterator end() const noexcept { return const_iterator(this); }

  reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }

  const_reverse_iterator rbegin() const noexcept {
    return const_reverse_iterator(end());
  }

  reverse_iterator rend() noexcept { return reverse_iterator(begin()); }

  const_reverse_iterator rend() const noexcept {
    return const_reverse_iterator(begin());
  }

  const_iterator cbegin() const noexcept { return begin(); }
  const_iterator cend() const noexcept { return end(); }

  const_reverse_iterator crbegin() const noexcept {
    return const_reverse_iterator(cend());
  }

  const_reverse_iterator crend() const noexcept {
    return const_reverse_iterator(cbegin());
  }

  bool empty() const noexcept { return size() == 0; }

  size_type size() const noexcept {
    return control_ ? control_->size : size_type(0);
  }

  size_type max_size() const noexcept {
    const auto allocator_max = allocator_traits_type::max_size(alloc_);
    using common_type = common_type_t<decltype(allocator_max), size_type>;
    return static_cast<size_type>(std::min<common_type>(
        allocator_max, static_max_size()));
  }

  size_type capacity() const noexcept {
    return control_ ? control_->capacity : size_type(0);
  }

  void reserve(size_type n) {
    if (n <= capacity()) {
      return;
    }
    capacity_plan plan;
    if (!plan_capacity(capacity(), n, plan)) {
      throw length_error("std::hive::reserve exceeds max_size");
    }
    reserve_blocks(plan);
    // No end epoch change: reserve preserves every iterator, including end.
  }

  void shrink_to_fit() {
    trim_capacity();
    if (capacity() == size() || size() == 0) {
      return;
    }

    capacity_plan plan;
    if (!plan_capacity(0, size(), plan)) {
      return;
    }
    if (plan.added >= capacity()) {
      return;
    }

    hive rebuilt(limits_, alloc_);
    // Finish every new-block allocation before moving a user value.  A failure
    // here leaves all remaining values unmoved; the earlier reserved-block
    // trim is an explicitly permitted reduction in capacity.
    rebuilt.reserve_blocks(plan);
    append_move_to(rebuilt);
    swap(rebuilt);
  }

  void trim_capacity() noexcept {
    control_type* const control = control_;
    if (!control) {
      return;
    }
    while (control->reserved_head) {
      block_type* const block = control->reserved_head;
      control->reserved_head = block->reserved_next;
      control->capacity -= block->capacity;
      deallocate_block(block);
    }
  }

  void trim_capacity(size_type n) noexcept {
    control_type* const control = control_;
    if (!control || n >= control->capacity) {
      return;
    }
    while (control->reserved_head &&
           control->capacity - control->reserved_head->capacity >= n) {
      block_type* const block = control->reserved_head;
      control->reserved_head = block->reserved_next;
      control->capacity -= block->capacity;
      deallocate_block(block);
    }
  }

  constexpr hive_limits block_capacity_limits() const noexcept {
    return limits_;
  }

  static constexpr hive_limits block_capacity_default_limits() noexcept {
    const hive_limits hard = block_capacity_hard_limits();
    const size_type minimum = hard.max < 8 ? hard.max : 8;
    const size_type maximum = hard.max < 4096 ? hard.max : 4096;
    return hive_limits(minimum, maximum);
  }

  static constexpr hive_limits block_capacity_hard_limits() noexcept {
    constexpr size_type maximum = std::min(
        static_max_size(), static_cast<size_type>(slot_npos));
    return hive_limits(maximum < 1 ? maximum : 1, maximum);
  }

  static constexpr bool is_within_hard_limits(hive_limits limits) noexcept {
    const hive_limits hard = block_capacity_hard_limits();
    return hard.min <= limits.min && limits.min <= limits.max &&
           limits.max <= hard.max;
  }

  void reshape(hive_limits block_limits) {
    validate_limits(block_limits);
    control_type* const control = control_;
    if (!control) {
      limits_ = block_limits;
      return;
    }

    bool rebuild = false;
    for (block_type* block = control->active_head; block;
         block = block->active_next) {
      if (!within(block_limits, block->capacity)) {
        rebuild = true;
        break;
      }
    }

    if (rebuild) {
      hive rebuilt(block_limits, alloc_);
      rebuilt.reserve(size());
      append_move_to(rebuilt);
      swap(rebuilt);
      return;
    }

    // Active elements need not move merely because reserved blocks are out of
    // bounds.  Drop precisely those reserved blocks and retain end/iterators.
    block_type** link = &control->reserved_head;
    while (*link) {
      block_type* const block = *link;
      if (!within(block_limits, block->capacity)) {
        *link = block->reserved_next;
        control->capacity -= block->capacity;
        deallocate_block(block);
      } else {
        link = &block->reserved_next;
      }
    }
    limits_ = block_limits;
  }

  template<class... Args>
  iterator emplace(Args&&... args) {
    control_type* control = control_;
    if (control && control->available_head) {
      block_type* const block = control->available_head;
      const bool reuse = block->free_head != npos;
      const size_type index =
          reuse ? block->free_head : block->high_water;
      const size_type run_length = reuse ? block->skip[index] : 0;
      const free_run_links links = reuse
          ? read_free_links(block, index)
          : free_run_links{slot_npos, slot_npos};
      try {
        allocator_traits_type::construct(
            alloc_, raw_element(block->slots[index]),
            std::forward<Args>(args)...);
      } catch (...) {
        // Construction may overwrite the dead storage holding the node.
        if (reuse) {
          write_free_links(block, index, links);
        }
        throw;
      }

      // After construction succeeds, the commit cannot throw.
      block->skip[index] = 0;
      if (reuse) {
        if (run_length == 1) {
          unlink_free_run(block, links);
        } else {
          const slot_index_type remaining =
              static_cast<slot_index_type>(run_length - 1);
          block->skip[index + 1] = remaining;
          block->skip[index + run_length - 1] = remaining;
          move_free_run(block, index + 1, links);
        }
      } else {
        ++block->high_water;
      }
      ++block->live_count;
      ++control->size;
      if (block->high_water == block->capacity &&
          block->free_head == npos) {
        available_remove(control, block);
      }
      touch_end();
      return iterator(block, index);
    }

    if (control && control->reserved_head) {
      block_type* const block = control->reserved_head;
      allocator_traits_type::construct(alloc_, raw_element(block->slots[0]),
                                       std::forward<Args>(args)...);

      control->reserved_head = block->reserved_next;
      block->reserved_next = nullptr;
      block->skip[0] = 0;
      block->high_water = 1;
      block->live_count = 1;
      active_append(control, block);
      if (block->high_water != block->capacity) {
        available_add(control, block);
      }
      ++control->size;
      touch_end();
      return iterator(block, 0);
    }

    const size_type current = capacity();
    const size_type maximum = max_size();
    const size_type block_max = allocation_block_max();
    if (limits_.min == 0 || block_max < limits_.min || current > maximum ||
        limits_.min > maximum - current) {
      throw length_error("std::hive::emplace exceeds max_size");
    }
    // Constant-work geometric growth from the capacity scalar, not a block
    // scan. Adding current doubles total capacity until a bound intervenes.
    const size_type block_capacity = std::min(
        {std::max(limits_.min, current), block_max, maximum - current});

    const bool new_control = control == nullptr;
    if (new_control) {
      control = allocate_control();
    }

    block_type* block;
    try {
      block = allocate_block(control, block_capacity);
    } catch (...) {
      if (new_control) {
        deallocate_control(control);
      }
      throw;
    }

    try {
      allocator_traits_type::construct(alloc_, raw_element(block->slots[0]),
                                       std::forward<Args>(args)...);
    } catch (...) {
      deallocate_block(block);
      if (new_control) {
        deallocate_control(control);
      }
      throw;
    }

    if (new_control) {
      control_ = control;
    }
    block->skip[0] = 0;
    block->high_water = 1;
    block->live_count = 1;
    active_append(control, block);
    if (block->high_water != block->capacity) {
      available_add(control, block);
    }
    control->capacity += block->capacity;
    ++control->size;
    touch_end();
    return iterator(block, 0);
  }

  template<class... Args>
  iterator emplace_hint(const_iterator hint, Args&&... args) {
    (void)hint;
    return emplace(std::forward<Args>(args)...);
  }

  iterator insert(const T& value) { return emplace(value); }
  iterator insert(T&& value) { return emplace(std::move(value)); }

  iterator insert(const_iterator hint, const T& value) {
    return emplace_hint(hint, value);
  }

  iterator insert(const_iterator hint, T&& value) {
    return emplace_hint(hint, std::move(value));
  }

  void insert(initializer_list<T> values) {
    insert(values.begin(), values.end());
  }

  template<__forge_hive_detail::compatible_range<T> R>
  void insert_range(R&& range) {
    append_range(range);
  }

  template<__forge_hive_detail::input_iterator InputIterator>
  void insert(InputIterator first, InputIterator last) {
    append_input(first, last);
  }

  void insert(size_type count, const T& value) { append_copies(count, value); }

  iterator erase(const_iterator position) {
    block_type* const block = position.block_;
    const size_type index = position.index_;
    const_iterator successor = position;
    ++successor;
    const bool erased_back = successor.block_ == nullptr;
    const size_type successor_index = successor.index_;
    block_type* const successor_block = successor.block_;
    control_type* const control = control_;
    slot_type& slot = block->slots[index];

    allocator_traits_type::destroy(alloc_, element(slot));
    --block->live_count;
    --control->size;

    if (index == block->high_water - 1) {
      block->high_water = index;
      if (index != 0 && block->skip[index - 1] != 0) {
        const size_type run_start = index - block->skip[index - 1];
        unlink_free_run(block, read_free_links(block, run_start));
        block->high_water = run_start;
      }
    } else {
      const size_type left = index == 0 ? 0 : block->skip[index - 1];
      const size_type right = block->skip[index + 1];
      const slot_index_type length =
          static_cast<slot_index_type>(left + right + 1);
      if (left == 0 && right == 0) {
        block->skip[index] = 1;
        push_free_run(block, index);
      } else if (right == 0) {
        block->skip[index - left] = length;
        block->skip[index] = length;
      } else if (left == 0) {
        const free_run_links links = read_free_links(block, index + 1);
        block->skip[index] = length;
        block->skip[index + right] = length;
        move_free_run(block, index, links);
      } else {
        block->skip[index - left] = length;
        block->skip[index + right] = length;
        block->skip[index] = 1;
        unlink_free_run(block, read_free_links(block, index + 1));
      }
    }

    if (block->live_count == 0) {
      available_remove(control, block);
      active_remove(control, block);
      reset_vacant_block(block);
      block->reserved_next = control->reserved_head;
      control->reserved_head = block;
    } else {
      if (!block->in_available) {
        available_add(control, block);
      }
    }

    if (erased_back) {
      touch_end();
      return end();
    }
    return iterator(successor_block, successor_index);
  }

  iterator erase(const_iterator first, const_iterator last) {
    if (!last.block_) {
      // Erasing the back changes end's epoch, so never loop against the old
      // cached end sentinel.
      while (first.block_) {
        first = erase(first);
      }
      return end();
    }

    block_type* const last_block = last.block_;
    const size_type last_index = last.index_;
    while (first.block_ != last_block || first.index_ != last_index) {
      first = erase(first);
    }
    return iterator(last_block, last_index);
  }

  void swap(hive& other) noexcept(pocs::value || always_equal::value) {
    if (this == &other) {
      return;
    }
    if constexpr (pocs::value) {
      using std::swap;
      swap(alloc_, other.alloc_);
    }
    std::swap(control_, other.control_);
    std::swap(limits_, other.limits_);
    if (control_) {
      control_->owner = this;
    }
    if (other.control_) {
      other.control_->owner = &other;
    }
    touch_end();
    other.touch_end();
  }

  void clear() noexcept {
    clear_without_end_invalidation();
    touch_end();
  }

  void splice(hive& source) {
    if (this == &source) {
      // Erroneous self-splice is diagnosed by a deliberate no-effect path.
      return;
    }

    control_type* const source_control = source.control_;
    if (!source_control || !source_control->active_head) {
      touch_end();
      source.touch_end();
      return;
    }

    size_type active_capacity = 0;
    for (block_type* block = source_control->active_head; block;
         block = block->active_next) {
      if (!within(limits_, block->capacity)) {
        throw length_error("std::hive::splice block outside current limits");
      }
      active_capacity += block->capacity;
    }
    // Validation and all allocation precede the first externally visible
    // change.  The commit below consists solely of pointer/integer updates.
    // splice has no max_size()-based length_error case: equal allocators may
    // legitimately report different implementation limits.
    const size_type moved_size = source_control->size;

    control_type* destination = control_;
    if (!destination && !source_control->reserved_head) {
      // With no source reserved blocks the entire stable control can migrate.
      // Source limits are intentionally not copied.
      control_ = source_control;
      source.control_ = nullptr;
      control_->owner = this;
      touch_end();
      source.touch_end();
      return;
    }

    if (!destination) {
      destination = allocate_control();
    }

    block_type* const first = source_control->active_head;
    block_type* const last = source_control->active_tail;
    if (destination->active_tail) {
      destination->active_tail->active_next = first;
      first->active_prev = destination->active_tail;
    } else {
      destination->active_head = first;
      first->active_prev = nullptr;
    }
    destination->active_tail = last;
    last->active_next = nullptr;

    for (block_type* block = first; block; block = block->active_next) {
      const bool available = block->in_available;
      block->available_prev = nullptr;
      block->available_next = nullptr;
      block->in_available = false;
      block->control = destination;
      block->order = destination->next_block_order++;
      if (available) {
        available_add(destination, block);
      }
    }

    source_control->active_head = nullptr;
    source_control->active_tail = nullptr;
    source_control->available_head = nullptr;
    source_control->size = 0;
    source_control->capacity -= active_capacity;
    destination->size += moved_size;
    destination->capacity += active_capacity;
    if (!control_) {
      control_ = destination;
    }

    touch_end();
    source.touch_end();
  }

  void splice(hive&& source) { splice(source); }

  template<class BinaryPredicate = equal_to<T>>
  size_type unique(BinaryPredicate binary_pred = BinaryPredicate()) {
    if (size() < 2) {
      return 0;
    }

    size_type removed = 0;
    const_iterator previous = begin();
    const_iterator current = previous;
    ++current;
    while (current != end()) {
      if (binary_pred(*current, *previous)) {
        current = erase(current);
        ++removed;
      } else {
        previous = current;
        ++current;
      }
    }
    return removed;
  }

  template<class Compare = less<T>>
  void sort(Compare comp = Compare()) {
    if (size() < 2) {
      return;
    }

    // Independent implementation of textbook indirect sorting and permutation
    // cycles (Knuth, TAOCP vol. 3, section 5.2). All comparisons finish before
    // container values change. Comparator-exception value retention remains an
    // observed property, not a stronger public exception guarantee.
    const size_type n = size();
    if constexpr (sort_uses_value_buffer) {
      using scratch_allocator =
          typename allocator_traits_type::template rebind_alloc<T>;
      using scratch_traits = allocator_traits<scratch_allocator>;
      scratch_allocator scratch(alloc_);
      const auto allocation = scratch_traits::allocate(scratch, n);
      struct release_buffer {
        scratch_allocator& allocator;
        typename scratch_traits::pointer allocation;
        size_type count;
        ~release_buffer() {
          scratch_traits::deallocate(allocator, allocation, count);
        }
      } release{scratch, allocation, n};
      T* const values = std::to_address(allocation);

      // memcpy starts the implicit-lifetime scratch objects. They are not
      // container elements, so allocator construct/destroy hooks do not run.
      // As with std::sort's own temporaries, comp may observe value copies.
      size_type index = 0;
      for (const T& value : *this) {
        std::memcpy(values + index++, std::addressof(value), sizeof(T));
      }
      std::sort(values, values + n, std::ref(comp));
      index = 0;
      for (T& value : *this) {
        value = values[index++];
      }
    } else {
      struct entry {
        T* address;
        size_type source;
      };
      using entry_allocator =
          typename allocator_traits_type::template rebind_alloc<entry>;
      std::vector<entry, entry_allocator> order{entry_allocator(alloc_)};
      order.reserve(n);
      for (T& value : *this) {
        order.push_back({std::addressof(value), order.size()});
      }
      std::sort(order.begin(), order.end(), [&comp](const entry& a, const entry& b) {
        return comp(*a.address, *b.address);
      });

      // order[d].source identifies the original slot whose value belongs at d.
      // Its address follows that same permutation. Swapping consecutive source
      // addresses closes each cycle in k-1 swaps; source == index marks visited.
      for (size_type first = 0; first < n; ++first) {
        size_type current = first;
        while (order[current].source != first) {
          const size_type next = order[current].source;
          using std::swap;
          swap(*order[current].address, *order[next].address);
          order[current].source = current;
          current = next;
        }
        order[current].source = current;
      }
    }
  }

  iterator get_iterator(const_pointer p) noexcept {
    const_iterator found = static_cast<const hive*>(this)->get_iterator(p);
    if (!found.block_) {
      return end();  // Precondition violation diagnostic, not an API result.
    }
    return iterator(found.block_, found.index_);
  }

  const_iterator get_iterator(const_pointer p) const noexcept {
    const T* const raw = std::to_address(p);
#if defined(FORGE_HIVE_TEST_HOOKS)
    lookup_work_ = {};
#endif
    const uintptr_t target = reinterpret_cast<uintptr_t>(raw);
    if (!control_) {
      return end();
    }

    // Supported platforms have flat, order-preserving uintptr_t addresses.
    // Integer arithmetic only proposes an index; a live-slot pointer equality
    // check confirms it. There is no unrelated-pointer subtraction or slot
    // scan, and no base-plus-allocation-size expression that can wrap.
    for (block_type* block = control_->active_head; block;
         block = block->active_next) {
#if defined(FORGE_HIVE_TEST_HOOKS)
      ++lookup_work_.first;
#endif
      const uintptr_t base = reinterpret_cast<uintptr_t>(block->slots);
      if (target < base) {
        continue;
      }
      const uintptr_t displacement = target - base;
      if (displacement % sizeof(slot_type) != offsetof(slot_type, storage)) {
        continue;
      }
      const uintptr_t candidate = displacement / sizeof(slot_type);
      if (candidate >= block->high_water) {
        continue;
      }
      const size_type index = static_cast<size_type>(candidate);
#if defined(FORGE_HIVE_TEST_HOOKS)
      ++lookup_work_.second;
#endif
      const slot_type& slot = block->slots[index];
      if (block->skip[index] == 0 && raw_element(slot) == raw) {
        return const_iterator(block, index);
      }
    }
    return end();  // p is required to denote a live element; no failure result.
  }

#if defined(FORGE_HIVE_TEST_HOOKS)
  // Define this macro uniformly in every linked translation unit that uses
  // the same hive specialization. No checks or test state exist without it.
  static constexpr bool __test_sort_uses_value_buffer() noexcept {
    return sort_uses_value_buffer;
  }

  // Last lookup's active blocks visited and candidate slots examined.
  pair<size_type, size_type> __test_lookup_work() const noexcept {
    return lookup_work_;
  }

  bool __test_invariants() const {
    if (!control_) {
      return true;
    }
    const control_type* const control = control_;
    if (control->owner != this || control->size > control->capacity) {
      return false;
    }

    size_type total_capacity = 0;
    size_type total_live = 0;
    size_type expected_available = 0;
    size_type active_count = 0;
    const auto valid_block = [&](const block_type* block) {
      return block->control == control && block->capacity != 0 &&
             block->capacity <= slot_npos &&
             block->capacity <= control->capacity - total_capacity &&
             to_address(block->block_allocation) == block &&
             block->slots && block->skip &&
             to_address(block->slot_allocation) == block->slots &&
             to_address(block->skip_allocation) == block->skip;
    };

    const block_type* previous_block = nullptr;
    for (const block_type* block = control->active_head; block;
         block = block->active_next) {
      if (!valid_block(block) || !block->active || block->reserved_next ||
          block->active_prev != previous_block ||
          (previous_block && previous_block->order >= block->order) ||
          block->live_count == 0 || block->high_water == 0 ||
          block->high_water > block->capacity ||
          block->skip[block->high_water - 1] != 0) {
        return false;
      }
      total_capacity += block->capacity;

      size_type live = 0;
      size_type runs = 0;
      size_type index = 0;
      while (index < block->high_water) {
        if (block->skip[index] == 0) {
          ++live;
          ++index;
          continue;
        }
        const size_type start = index;
        do {
          ++index;
        } while (index < block->high_water && block->skip[index] != 0);
        const size_type length = index - start;
        if (index == block->high_water || block->skip[start] != length ||
            block->skip[index - 1] != length) {
          return false;
        }
        ++runs;
      }
      if (live != block->live_count || live > control->size - total_live) {
        return false;
      }
      total_live += live;

      // Run heads must occur exactly once, with reciprocal free-list links.
      // The run count bounds this walk even if a damaged list contains a cycle.
      size_type nodes = 0;
      size_type previous_index = npos;
      for (size_type start = block->free_head; start != npos;) {
        if (nodes == runs || start >= block->high_water ||
            block->skip[start] == 0 ||
            (start != 0 && block->skip[start - 1] != 0)) {
          return false;
        }
        const free_run_links links = read_free_links(block, start);
        if (to_wide_index(links.previous) != previous_index) {
          return false;
        }
        ++nodes;
        previous_index = start;
        start = to_wide_index(links.next);
      }
      if (nodes != runs) {
        return false;
      }

      const bool available = block->free_head != npos ||
                             block->high_water < block->capacity;
      if (block->in_available != available ||
          (!available && (block->available_prev || block->available_next))) {
        return false;
      }
      expected_available += available ? 1 : 0;
      ++active_count;
      previous_block = block;
    }
    if (previous_block != control->active_tail || total_live != control->size) {
      return false;
    }

    // Positive capacities bound both block walks by the stored total capacity.
    // Reserved flags also make the active and reserved sets disjoint.
    for (const block_type* block = control->reserved_head; block;
         block = block->reserved_next) {
      if (!valid_block(block) || block->active || block->in_available ||
          block->live_count != 0 || block->high_water != 0 ||
          block->free_head != npos || block->active_prev ||
          block->active_next || block->available_prev || block->available_next) {
        return false;
      }
      total_capacity += block->capacity;
    }
    if (total_capacity != control->capacity) {
      return false;
    }

    size_type available_count = 0;
    previous_block = nullptr;
    for (const block_type* block = control->available_head; block;
         block = block->available_next) {
      if (available_count == expected_available) {
        return false;
      }
      // Check actual membership without allocating scratch state. Only block
      // lists are rescanned; the skipfield and free runs each get one pass.
      const block_type* member = control->active_head;
      size_type examined = 0;
      while (member && member != block && examined < active_count) {
        member = member->active_next;
        ++examined;
      }
      if (member != block || !block->in_available ||
          block->available_prev != previous_block) {
        return false;
      }
      ++available_count;
      previous_block = block;
    }
    return available_count == expected_available;
  }
#endif

 private:
  void append_move_to(hive& destination) {
    auto current = begin();
    const auto last = end();
    while (current != last) {
      destination.emplace(std::move(*current));
      ++current;
    }
  }

  allocator_type alloc_;
  hive_limits limits_ = block_capacity_default_limits();
  control_type* control_ = nullptr;
  size_type end_epoch_ = 0;
#if defined(FORGE_HIVE_TEST_HOOKS)
  mutable pair<size_type, size_type> lookup_work_{};
#endif
};

// CTAD participation follows [sequence.reqmts]: non-input iterators and
// non-allocators are excluded before a hive specialization can be formed.
template<__forge_hive_detail::input_iterator InputIterator,
         __forge_hive_detail::allocator Allocator =
             allocator<iter_value_t<InputIterator>>>
hive(InputIterator, InputIterator, Allocator = Allocator())
    -> hive<iter_value_t<InputIterator>, Allocator>;

template<__forge_hive_detail::input_iterator InputIterator,
         __forge_hive_detail::allocator Allocator =
             allocator<iter_value_t<InputIterator>>>
hive(InputIterator, InputIterator, hive_limits, Allocator = Allocator())
    -> hive<iter_value_t<InputIterator>, Allocator>;

template<ranges::input_range R,
         __forge_hive_detail::allocator Allocator =
             allocator<ranges::range_value_t<R>>>
hive(from_range_t, R&&, Allocator = Allocator())
    -> hive<ranges::range_value_t<R>, Allocator>;

template<ranges::input_range R,
         __forge_hive_detail::allocator Allocator =
             allocator<ranges::range_value_t<R>>>
hive(from_range_t, R&&, hive_limits, Allocator = Allocator())
    -> hive<ranges::range_value_t<R>, Allocator>;

template<class T, class Allocator>
void swap(hive<T, Allocator>& left,
          hive<T, Allocator>& right) noexcept(noexcept(left.swap(right))) {
  left.swap(right);
}

template<class T, class Allocator, class U = T>
typename hive<T, Allocator>::size_type erase(hive<T, Allocator>& container,
                                             const U& value);

template<class T, class Allocator, class Predicate>
typename hive<T, Allocator>::size_type erase_if(
    hive<T, Allocator>& container, Predicate pred) {
  const auto original_size = container.size();
  for (auto current = container.begin(); current != container.end();) {
    if (pred(*current)) {
      current = container.erase(current);
    } else {
      ++current;
    }
  }
  return original_size - container.size();
}

template<class T, class Allocator, class U>
typename hive<T, Allocator>::size_type erase(hive<T, Allocator>& container,
                                             const U& value) {
  return erase_if(container, [&](const auto& element) -> bool {
    return element == value;
  });
}

namespace pmr {
template<class T>
using hive = std::hive<T, polymorphic_allocator<T>>;
} // namespace pmr

} // namespace std

#if !defined(__cpp_lib_hive)
#  define __cpp_lib_hive 202502L
#endif

#endif // Forge hive injection guard
