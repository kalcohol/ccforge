#ifndef FORGE_TEST_HIVE_TEST_ALLOCATOR_HPP
#define FORGE_TEST_HIVE_TEST_ALLOCATOR_HPP

#include <algorithm>
#include <cstddef>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>

namespace forge_hive_allocator_test {

struct allocation_record {
    std::size_t bytes;
    std::size_t alignment;
    int owner_id;
    std::type_index allocated_type;
};

struct allocation_state {
    std::unordered_map<void*, allocation_record> allocations;
    std::size_t allocation_attempts = 0;
    std::unordered_map<std::type_index, std::size_t> attempts_by_type;
    // Used by the splice regression test.  Only allocator<int> observes this
    // limit; block/slot/control rebinds and other element types never do.
    std::optional<std::size_t> artificial_int_max_size;
    // -1 means unlimited, zero fails the next allocation, and a positive value
    // is decremented immediately before each successful underlying allocation.
    long long fail_after = -1;
    bool valid_deallocations = true;

    bool clean() const { return valid_deallocations && allocations.empty(); }

    std::size_t count_for_id(int id) const {
        std::size_t result = 0;
        for (const auto& [pointer, record] : allocations) {
            (void)pointer;
            if (record.owner_id == id) ++result;
        }
        return result;
    }

    template<class T>
    std::size_t current_allocations_for() const {
        std::size_t result = 0;
        for (const auto& [pointer, record] : allocations) {
            (void)pointer;
            if (record.allocated_type == std::type_index(typeid(T))) ++result;
        }
        return result;
    }

    template<class T>
    std::size_t allocation_attempts_for() const {
        const auto found = attempts_by_type.find(std::type_index(typeid(T)));
        return found == attempts_by_type.end() ? 0 : found->second;
    }
};

template<class T,
         bool POCCA = false,
         bool POCMA = false,
         bool POCS = false,
         bool AlwaysEqual = false>
class stateful_allocator {
    template<class, bool, bool, bool, bool>
    friend class stateful_allocator;

public:
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using propagate_on_container_copy_assignment = std::bool_constant<POCCA>;
    using propagate_on_container_move_assignment = std::bool_constant<POCMA>;
    using propagate_on_container_swap = std::bool_constant<POCS>;
    using is_always_equal = std::bool_constant<AlwaysEqual>;

    template<class U>
    struct rebind {
        using other = stateful_allocator<U, POCCA, POCMA, POCS, AlwaysEqual>;
    };

    stateful_allocator()
        : state_(std::make_shared<allocation_state>()), id_(0) {}
    explicit stateful_allocator(std::shared_ptr<allocation_state> state, int id)
        : state_(std::move(state)), id_(id) {}

    template<class U>
    stateful_allocator(
        const stateful_allocator<U, POCCA, POCMA, POCS, AlwaysEqual>& other) noexcept
        : state_(other.state_), id_(other.id_) {}

    stateful_allocator select_on_container_copy_construction() const {
        return stateful_allocator(state_, id_ + 100);
    }

    T* allocate(std::size_t count) {
        if (count > max_size()) throw std::bad_array_new_length();
        if (state_->fail_after == 0) throw std::bad_alloc();
        if (state_->fail_after > 0) --state_->fail_after;
        ++state_->allocation_attempts;
        ++state_->attempts_by_type[std::type_index(typeid(T))];
        const std::size_t bytes = count * sizeof(T);
        void* const result = ::operator new(bytes, std::align_val_t{alignof(T)});
        state_->allocations.emplace(
            result, allocation_record{bytes, alignof(T), id_, std::type_index(typeid(T))});
        return static_cast<T*>(result);
    }

    void deallocate(T* pointer, std::size_t count) noexcept {
        if (pointer == nullptr) {
            state_->valid_deallocations = false;
            return;
        }
        const auto found = state_->allocations.find(pointer);
        if (found == state_->allocations.end()) {
            state_->valid_deallocations = false;
            return;
        }
        const auto record = found->second;
        if ((!AlwaysEqual && record.owner_id != id_) ||
            record.bytes != count * sizeof(T) ||
            record.alignment != alignof(T) ||
            record.allocated_type != std::type_index(typeid(T))) {
            state_->valid_deallocations = false;
        }
        state_->allocations.erase(found);
        ::operator delete(pointer, std::align_val_t{record.alignment});
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
    const std::shared_ptr<allocation_state>& state() const noexcept { return state_; }

    friend bool operator==(const stateful_allocator& left,
                           const stateful_allocator& right) noexcept {
        if constexpr (AlwaysEqual) {
            return true;
        } else {
            return left.state_ == right.state_ && left.id_ == right.id_;
        }
    }

private:
    std::shared_ptr<allocation_state> state_;
    int id_;
};

} // namespace forge_hive_allocator_test

#endif
