#ifndef FORGE_TEST_HIVE_TEST_FANCY_ALLOCATOR_HPP
#define FORGE_TEST_HIVE_TEST_FANCY_ALLOCATOR_HPP

#include "hive_test_allocator.hpp"

#include <algorithm>
#include <compare>
#include <concepts>
#include <cstddef>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <type_traits>
#include <typeindex>
#include <utility>

namespace forge_hive_fancy_allocator_test {

// A real non-raw allocator pointer.  The arithmetic is also sufficient for
// libstdc++/libc++ vector iterators, so hive::sort instantiates its ordinary
// std::vector<T, allocator_type> temporary instead of needing a test shortcut.
template<class T>
class fancy_ptr {
    template<class>
    friend class fancy_ptr;

public:
    using element_type = T;
    using value_type = std::remove_cv_t<T>;
    using difference_type = std::ptrdiff_t;
    using pointer = fancy_ptr;
    using reference = std::add_lvalue_reference_t<T>;
    using iterator_category = std::random_access_iterator_tag;
    using iterator_concept = std::random_access_iterator_tag;

    template<class U>
    using rebind = fancy_ptr<U>;

    constexpr fancy_ptr() noexcept = default;
    constexpr fancy_ptr(std::nullptr_t) noexcept {}
    constexpr explicit fancy_ptr(T* value) noexcept : value_(value) {}

    template<class U>
        requires std::convertible_to<U*, T*>
    constexpr fancy_ptr(const fancy_ptr<U>& other) noexcept : value_(other.value_) {}

    template<class U>
        requires(std::is_void_v<U> && !std::is_void_v<T> &&
                 requires(U* value) { static_cast<T*>(value); })
    constexpr explicit fancy_ptr(const fancy_ptr<U>& other) noexcept
        : value_(static_cast<T*>(other.value_)) {}

    constexpr T* get() const noexcept { return value_; }
    constexpr T* operator->() const noexcept { return value_; }
    constexpr reference operator*() const { return *value_; }
    constexpr reference operator[](difference_type offset) const { return value_[offset]; }
    constexpr explicit operator bool() const noexcept { return value_ != nullptr; }

    template<class U = T>
        requires(!std::is_void_v<U>)
    static constexpr fancy_ptr pointer_to(U& value) noexcept {
        return fancy_ptr(std::addressof(value));
    }

    constexpr fancy_ptr& operator++() {
        ++value_;
        return *this;
    }
    constexpr fancy_ptr operator++(int) {
        auto copy = *this;
        ++*this;
        return copy;
    }
    constexpr fancy_ptr& operator--() {
        --value_;
        return *this;
    }
    constexpr fancy_ptr operator--(int) {
        auto copy = *this;
        --*this;
        return copy;
    }
    constexpr fancy_ptr& operator+=(difference_type offset) {
        value_ += offset;
        return *this;
    }
    constexpr fancy_ptr& operator-=(difference_type offset) {
        value_ -= offset;
        return *this;
    }

    friend constexpr fancy_ptr operator+(fancy_ptr pointer, difference_type offset) {
        pointer += offset;
        return pointer;
    }
    friend constexpr fancy_ptr operator+(difference_type offset, fancy_ptr pointer) {
        pointer += offset;
        return pointer;
    }
    friend constexpr fancy_ptr operator-(fancy_ptr pointer, difference_type offset) {
        pointer -= offset;
        return pointer;
    }
    friend constexpr difference_type operator-(fancy_ptr left, fancy_ptr right) {
        return left.value_ - right.value_;
    }
    friend constexpr bool operator==(fancy_ptr left, fancy_ptr right) noexcept {
        return left.value_ == right.value_;
    }
    friend constexpr std::strong_ordering operator<=>(
        fancy_ptr left, fancy_ptr right) noexcept {
        return left.value_ <=> right.value_;
    }

private:
    T* value_ = nullptr;
};

} // namespace forge_hive_fancy_allocator_test

namespace std {

template<class T>
struct pointer_traits<forge_hive_fancy_allocator_test::fancy_ptr<T>> {
    using pointer = forge_hive_fancy_allocator_test::fancy_ptr<T>;
    using element_type = T;
    using difference_type = ptrdiff_t;

    template<class U>
    using rebind = forge_hive_fancy_allocator_test::fancy_ptr<U>;

    static constexpr element_type* to_address(const pointer& value) noexcept {
        return value.get();
    }

    template<class U = T>
        requires(!is_void_v<U>)
    static constexpr pointer pointer_to(U& value) noexcept {
        return pointer::pointer_to(value);
    }
};

} // namespace std

namespace forge_hive_fancy_allocator_test {

template<class T>
class fancy_allocator {
    template<class>
    friend class fancy_allocator;

public:
    using value_type = T;
    using pointer = fancy_ptr<T>;
    using const_pointer = fancy_ptr<const T>;
    using void_pointer = fancy_ptr<void>;
    using const_void_pointer = fancy_ptr<const void>;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using propagate_on_container_copy_assignment = std::false_type;
    using propagate_on_container_move_assignment = std::true_type;
    using propagate_on_container_swap = std::true_type;
    using is_always_equal = std::false_type;

    template<class U>
    struct rebind {
        using other = fancy_allocator<U>;
    };

    fancy_allocator()
        : state_(std::make_shared<forge_hive_allocator_test::allocation_state>()), id_(0) {}
    explicit fancy_allocator(
        std::shared_ptr<forge_hive_allocator_test::allocation_state> state, int id)
        : state_(std::move(state)), id_(id) {}

    template<class U>
    fancy_allocator(const fancy_allocator<U>& other) noexcept
        : state_(other.state_), id_(other.id_) {}

    pointer allocate(std::size_t count) {
        if (count > max_size()) throw std::bad_array_new_length();
        if (state_->fail_after == 0) throw std::bad_alloc();
        if (state_->fail_after > 0) --state_->fail_after;
        ++state_->allocation_attempts;
        ++state_->attempts_by_type[std::type_index(typeid(T))];
        const std::size_t bytes = count * sizeof(T);
        void* const result = ::operator new(bytes, std::align_val_t{alignof(T)});
        state_->allocations.emplace(
            result,
            forge_hive_allocator_test::allocation_record{
                bytes, alignof(T), id_, std::type_index(typeid(T))});
        return pointer(static_cast<T*>(result));
    }

    void deallocate(pointer pointer_value, std::size_t count) noexcept {
        T* const raw = pointer_value.get();
        if (raw == nullptr) {
            state_->valid_deallocations = false;
            return;
        }
        const auto found = state_->allocations.find(raw);
        if (found == state_->allocations.end()) {
            state_->valid_deallocations = false;
            return;
        }
        const auto record = found->second;
        if (record.owner_id != id_ || record.bytes != count * sizeof(T) ||
            record.alignment != alignof(T) ||
            record.allocated_type != std::type_index(typeid(T))) {
            state_->valid_deallocations = false;
        }
        state_->allocations.erase(found);
        ::operator delete(raw, std::align_val_t{record.alignment});
    }

    std::size_t max_size() const noexcept {
        const std::size_t ordinary = std::numeric_limits<std::size_t>::max() / sizeof(T);
        if constexpr (std::is_same_v<std::remove_cv_t<T>, int>) {
            if (state_ && state_->artificial_int_max_size) {
                return std::min(ordinary, *state_->artificial_int_max_size);
            }
        }
        return ordinary;
    }

    int id() const noexcept { return id_; }
    const std::shared_ptr<forge_hive_allocator_test::allocation_state>& state() const noexcept {
        return state_;
    }

    friend bool operator==(const fancy_allocator& left,
                           const fancy_allocator& right) noexcept {
        return left.state_ == right.state_ && left.id_ == right.id_;
    }

private:
    std::shared_ptr<forge_hive_allocator_test::allocation_state> state_;
    int id_;
};

static_assert(std::random_access_iterator<fancy_ptr<int>>);
static_assert(std::random_access_iterator<fancy_ptr<const int>>);
static_assert(std::is_same_v<
              std::allocator_traits<fancy_allocator<int>>::pointer,
              fancy_ptr<int>>);
static_assert(std::is_same_v<
              std::allocator_traits<fancy_allocator<int>>::const_pointer,
              fancy_ptr<const int>>);
static_assert(std::is_same_v<
              std::allocator_traits<fancy_allocator<int>>::rebind_alloc<double>,
              fancy_allocator<double>>);

template<class A>
concept allocator_void_roundtrip = requires(
    typename std::allocator_traits<A>::pointer p,
    typename std::allocator_traits<A>::const_pointer cp,
    typename std::allocator_traits<A>::void_pointer v,
    typename std::allocator_traits<A>::const_void_pointer cv) {
    { static_cast<typename std::allocator_traits<A>::void_pointer>(p) };
    { static_cast<typename std::allocator_traits<A>::const_void_pointer>(cp) };
    { static_cast<typename std::allocator_traits<A>::pointer>(v) };
    { static_cast<typename std::allocator_traits<A>::const_pointer>(cv) };
};

static_assert(allocator_void_roundtrip<std::allocator<int>>);
static_assert(allocator_void_roundtrip<fancy_allocator<int>>);
static_assert(std::is_convertible_v<fancy_ptr<int>, fancy_ptr<void>>);
static_assert(std::is_convertible_v<fancy_ptr<const int>, fancy_ptr<const void>>);
static_assert(std::is_constructible_v<fancy_ptr<const int>, fancy_ptr<void>>);
static_assert(!std::is_convertible_v<fancy_ptr<void>, fancy_ptr<int>>);
static_assert(!std::is_convertible_v<fancy_ptr<const void>, fancy_ptr<const int>>);
static_assert(!std::is_constructible_v<fancy_ptr<int>, fancy_ptr<const void>>);
static_assert(!std::is_constructible_v<fancy_ptr<void>, fancy_ptr<const int>>);

} // namespace forge_hive_fancy_allocator_test

#endif
