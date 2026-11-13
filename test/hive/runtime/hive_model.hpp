// Independent public-semantics model for std::hive.
//
// The model deliberately has no knowledge of blocks, slot indexes, capacity
// growth, insertion position, or free-slot selection.  It is shared by the
// GoogleTest runtime test and the scratch implementation-mutation control.
#ifndef FORGE_TEST_HIVE_MODEL_HPP
#define FORGE_TEST_HIVE_MODEL_HPP

#include <hive>

#include <algorithm>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <list>
#include <memory>
#include <random>
#include <ranges>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace forge_hive_model_test {

struct element {
    int id = -1;
    int payload = 0;
};

using hive_type = std::hive<element>;
using order_type = std::list<int>;
using address_map = std::unordered_map<int, const element*>;

inline std::vector<int> as_vector(const order_type& order) {
    return {order.begin(), order.end()};
}

inline bool fail(std::string* error, const std::string& message) {
    if (error != nullptr && error->empty()) {
        *error = "model mismatch: " + message;
    }
    return false;
}

inline bool check(const hive_type& actual,
                  const order_type& order,
                  const address_map& addresses,
                  std::string* error) {
    if (actual.size() != order.size()) {
        std::ostringstream message;
        message << "size " << actual.size() << " != live model size "
                << order.size();
        return fail(error, message.str());
    }
    if (actual.empty() != order.empty()) {
        return fail(error, "empty disagrees with live model");
    }
    if (actual.capacity() < actual.size()) {
        return fail(error, "capacity is smaller than size");
    }
    if (actual.max_size() < actual.size()) {
        return fail(error, "max_size is smaller than size");
    }
    if (static_cast<std::size_t>(std::distance(actual.begin(), actual.end())) !=
        order.size()) {
        return fail(error, "forward distance differs from size");
    }
    if (order.empty()) {
        if (actual.begin() != actual.end() || actual.rbegin() != actual.rend()) {
            return fail(error, "empty container has an iterable element");
        }
    }

    std::vector<int> actual_ids;
    actual_ids.reserve(order.size());
    std::set<int> unique_ids;
    std::set<const element*> unique_addresses;
    const auto& const_actual = actual;
    for (auto it = actual.begin(); it != actual.end(); ++it) {
        const int id = it->id;
        actual_ids.push_back(id);
        if (!unique_ids.insert(id).second) {
            return fail(error, "an element identity was visited more than once");
        }
        const auto model_address = addresses.find(id);
        if (model_address == addresses.end()) {
            return fail(error, "iteration exposed an unknown or dead identity");
        }
        const element* const address = std::addressof(*it);
        if (address != model_address->second) {
            return fail(error, "an operation changed a live element address");
        }
        if (!unique_addresses.insert(address).second) {
            return fail(error, "two identities share one live element address");
        }
        if (actual.get_iterator(address) != it) {
            return fail(error, "get_iterator does not recover element ownership");
        }
        if (const_actual.get_iterator(address) != typename hive_type::const_iterator(it)) {
            return fail(error, "const get_iterator does not recover ownership");
        }

        auto next = std::next(it);
        if (next != actual.end()) {
            const typename hive_type::const_iterator const_it = it;
            const typename hive_type::const_iterator const_next = next;
            if (!(it < next) || !(next > it) || !(it <= next) || !(next >= it) ||
                (it <=> next) != std::strong_ordering::less ||
                (const_next <=> it) != std::strong_ordering::greater ||
                !(const_it < next) || !(it < const_next)) {
                return fail(error, "iterator ordering disagrees with iteration order");
            }
        }
    }
    if (actual_ids != as_vector(order)) {
        return fail(error, "forward iteration differs from the specified sequence order");
    }

    auto model_reverse = order.rbegin();
    for (auto it = actual.rbegin(); it != actual.rend(); ++it, ++model_reverse) {
        if (model_reverse == order.rend() || it->id != *model_reverse) {
            return fail(error, "reverse iteration differs from reversed sequence order");
        }
    }
    if (model_reverse != order.rend()) {
        return fail(error, "reverse iteration omitted a live element");
    }

    if (!order.empty()) {
        auto last = actual.end();
        --last;
        if (last->id != order.back()) {
            return fail(error, "--end is not the final live sequence element");
        }
    }
    return true;
}

struct fixture {
    hive_type actual;
    order_type order;
    address_map addresses;
    int next_id = 0;

    element make_element(int salt = 0) {
        return element{next_id++, next_id * 17 + salt};
    }

    bool check_after(const char* operation, std::uint64_t step, std::string* error) const {
        if (check(actual, order, addresses, error)) {
            return true;
        }
        if (error != nullptr) {
            std::ostringstream suffix;
            suffix << " (operation=" << operation << ", step=" << step << ')';
            *error += suffix.str();
        }
        return false;
    }

    // Insert position and free-slot choice are intentionally not modeled as
    // push_back.  The standard permits reuse to affect sequence position.  We
    // check the invariants that are specified (old relative order, identities,
    // addresses and exactly-once liveness), then record the publicly observed
    // position for subsequent erase/iteration checks.
    bool accept_insertions(const std::vector<int>& inserted_ids,
                           const char* operation, std::uint64_t step,
                           std::string* error) {
        std::set<int> new_ids(inserted_ids.begin(), inserted_ids.end());
        if (new_ids.size() != inserted_ids.size()) {
            return fail(error, "test attempted to insert duplicate model identities");
        }
        const std::set<int> old_ids(order.begin(), order.end());
        std::set<int> seen_old;
        std::set<int> seen_new;
        std::vector<int> observed;
        std::unordered_map<int, const element*> observed_addresses;
        for (auto it = actual.begin(); it != actual.end(); ++it) {
            const int id = it->id;
            observed.push_back(id);
            observed_addresses.emplace(id, std::addressof(*it));
            if (new_ids.contains(id)) {
                if (!seen_new.insert(id).second) {
                    return fail(error, "a newly inserted identity was duplicated");
                }
            } else if (old_ids.contains(id)) {
                if (!seen_old.insert(id).second) {
                    return fail(error, "a pre-existing identity was duplicated");
                }
                if (addresses.at(id) != std::addressof(*it)) {
                    return fail(error, "insertion moved a pre-existing element");
                }
            } else {
                return fail(error, "insertion exposed an unknown or dead identity");
            }
        }
        if (seen_old != old_ids || seen_new != new_ids) {
            return fail(error, "insertion lost an old or new live identity");
        }
        auto old_position = order.begin();
        for (const int id : observed) {
            if (old_position != order.end() && id == *old_position) {
                ++old_position;
            }
        }
        if (old_position != order.end()) {
            return fail(error, "insertion changed pre-existing relative sequence order");
        }
        for (const int id : inserted_ids) {
            addresses[id] = observed_addresses.at(id);
        }
        order.assign(observed.begin(), observed.end());
        return check_after(operation, step, error);
    }

    bool inserted(element value, const char* operation, std::uint64_t step,
                  std::string* error) {
        const int id = value.id;
        auto it = actual.emplace(value);
        if (it->id != id) {
            return fail(error, "emplace returned a different element identity");
        }
        return accept_insertions({id}, operation, step, error);
    }

    bool erase_one(std::size_t index, std::uint64_t step, std::string* error) {
        auto position = actual.begin();
        std::advance(position, static_cast<hive_type::difference_type>(index));
        auto model_position = order.begin();
        std::advance(model_position, static_cast<order_type::difference_type>(index));
        const int erased_id = *model_position;
        auto successor = std::next(position);
        const bool erasing_last = successor == actual.end();
        int successor_id = -1;
        if (!erasing_last) {
            successor_id = successor->id;
        }

        auto returned = actual.erase(position);
        if (erasing_last) {
            // The old end was invalidated by erasing the last sequence element.
            // Only the newly returned end and a fresh end observer are used.
            if (returned != actual.end()) {
                return fail(error, "erasing the last element did not return end");
            }
        } else {
            if (returned == actual.end() || returned->id != successor_id) {
                return fail(error, "erase did not return the original logical successor");
            }
        }
        addresses.erase(erased_id);
        order.erase(model_position);
        return check_after("erase(one)", step, error);
    }

    bool erase_range(std::size_t first_index, std::size_t last_index,
                     std::uint64_t step, std::string* error) {
        auto first = actual.begin();
        auto last = actual.begin();
        std::advance(first, static_cast<hive_type::difference_type>(first_index));
        std::advance(last, static_cast<hive_type::difference_type>(last_index));
        auto model_first = order.begin();
        auto model_last = order.begin();
        std::advance(model_first, static_cast<order_type::difference_type>(first_index));
        std::advance(model_last, static_cast<order_type::difference_type>(last_index));
        std::vector<int> erased_ids(model_first, model_last);
        const bool suffix = last == actual.end();
        int successor_id = -1;
        if (!suffix) {
            successor_id = last->id;
        }

        auto returned = actual.erase(first, last);
        if (suffix) {
            if (returned != actual.end()) {
                return fail(error, "suffix erase did not return the new end");
            }
        } else if (returned == actual.end() || returned->id != successor_id) {
            return fail(error, "range erase did not return its original last iterator");
        }
        for (const int id : erased_ids) {
            addresses.erase(id);
        }
        order.erase(model_first, model_last);
        return check_after("erase(range)", step, error);
    }
};

inline bool run(std::uint32_t seed, std::uint64_t steps, std::string* error) {
    fixture f;
    std::mt19937 random(seed);

    for (std::uint64_t step = 0; step < steps; ++step) {
        // Establish many live elements before heavily exercising holes.  This
        // gives broad cross-block coverage without assuming a block size.
        unsigned choice = random() % 100;
        if (step < 96 || f.actual.size() < 6) {
            choice = random() % 4;
        }

        if (choice < 28) {
            const element value = f.make_element();
            const int id = value.id;
            auto it = f.actual.emplace(value);
            if (it->id != id) {
                return fail(error, "emplace returned the wrong identity");
            }
            if (!f.accept_insertions({id}, "emplace", step, error)) return false;
        } else if (choice < 40) {
            const element value = f.make_element(1);
            const int id = value.id;
            auto it = f.actual.insert(value);
            if (it->id != id) {
                return fail(error, "insert(const T&) returned the wrong identity");
            }
            if (!f.accept_insertions({id}, "insert(const T&)", step, error)) return false;
        } else if (choice < 49) {
            element value = f.make_element(2);
            const int id = value.id;
            auto it = f.actual.insert(std::move(value));
            if (it->id != id) {
                return fail(error, "insert(T&&) returned the wrong identity");
            }
            if (!f.accept_insertions({id}, "insert(T&&)", step, error)) return false;
        } else if (choice < 55) {
            const element value = f.make_element(3);
            const int id = value.id;
            const auto hint = f.actual.empty() ? f.actual.end() : f.actual.begin();
            auto it = f.actual.emplace_hint(hint, value);
            if (it->id != id) {
                return fail(error, "emplace_hint returned the wrong identity");
            }
            if (!f.accept_insertions({id}, "emplace_hint", step, error)) return false;
        } else if (choice < 61) {
            std::vector<element> input;
            input.emplace_back(f.make_element(4));
            input.emplace_back(f.make_element(5));
            input.emplace_back(f.make_element(6));
            f.actual.insert(input.begin(), input.end());
            const std::vector<int> ids{input[0].id, input[1].id, input[2].id};
            if (!f.accept_insertions(ids, "insert(iterator range)", step, error)) {
                return false;
            }
        } else if (choice < 65) {
            const element value = f.make_element(7);
            f.actual.insert(std::size_t{1}, value);
            if (!f.accept_insertions({value.id}, "insert(fill, one)", step, error)) {
                return false;
            }
        } else if (choice < 88 && !f.actual.empty()) {
            const std::size_t index = random() % f.actual.size();
            if (!f.erase_one(index, step, error)) return false;
        } else if (choice < 94 && f.actual.size() >= 2) {
            const std::size_t first = random() % (f.actual.size() - 1);
            const std::size_t last = first + 1 + random() % (f.actual.size() - first);
            if (!f.erase_range(first, last, step, error)) return false;
        } else if (choice == 94) {
            f.actual.reserve(f.actual.capacity() + 1 + random() % 5);
            if (!f.check_after("reserve", step, error)) return false;
        } else if (choice == 95) {
            const auto target = f.actual.size() +
                (f.actual.capacity() > f.actual.size()
                     ? random() % (f.actual.capacity() - f.actual.size() + 1)
                     : 0);
            f.actual.trim_capacity(target);
            if (!f.check_after("trim_capacity(n)", step, error)) return false;
        } else if (choice == 96) {
            f.actual.trim_capacity();
            if (!f.check_after("trim_capacity()", step, error)) return false;
        } else if (choice == 97 && f.actual.size() > 12) {
            f.actual.clear();
            f.order.clear();
            f.addresses.clear();
            if (!f.check_after("clear", step, error)) return false;
        } else {
            const element value = f.make_element(8);
            f.actual.insert_range(std::vector<element>{value});
            if (!f.accept_insertions({value.id}, "insert_range", step, error)) {
                return false;
            }
        }
    }

    // Deterministic hole/reuse pass: alternating erasure followed by insertion
    // makes duplicate-free-slot and erased-slot visitation defects observable.
    for (int round = 0; round < 3; ++round) {
        for (int index = 0; index < 48; ++index) {
            const element value = f.make_element(100 + round);
            const int id = value.id;
            auto it = f.actual.emplace(value);
            if (it->id != id) {
                return fail(error, "deterministic emplace returned the wrong identity");
            }
            if (!f.accept_insertions({id}, "deterministic fill", steps + round, error)) {
                return false;
            }
        }
        std::size_t index = 0;
        while (index < f.actual.size()) {
            if (!f.erase_one(index, steps + 10 + round, error)) return false;
            ++index;
        }
    }
    return true;
}

} // namespace forge_hive_model_test

#endif
