#include <hive>

#include "../runtime/hive_test_fancy_allocator.hpp"

#include <memory>
#include <type_traits>
#include <utility>

using allocator_type = forge_hive_fancy_allocator_test::fancy_allocator<int>;
using hive_type = std::hive<int, allocator_type>;
using state_type = forge_hive_allocator_test::allocation_state;

static_assert(std::is_same_v<typename hive_type::pointer,
              forge_hive_fancy_allocator_test::fancy_ptr<int>>);
static_assert(std::is_same_v<typename hive_type::const_pointer,
              forge_hive_fancy_allocator_test::fancy_ptr<const int>>);

void instantiate_fancy_pointer_api() {
    auto state = std::make_shared<state_type>();
    hive_type hive(allocator_type(state, 1));
    hive.emplace(3);
    hive.emplace(1);
    typename hive_type::const_pointer pointer =
        std::pointer_traits<typename hive_type::const_pointer>::pointer_to(
            std::as_const(*hive.begin()));
    (void)hive.get_iterator(pointer);
    const auto& const_hive = hive;
    (void)const_hive.get_iterator(pointer);

    hive_type moved(std::move(hive));
    hive_type other(allocator_type(state, 2));
    other.emplace(2);
    moved.swap(other); // POCS=true.

    hive_type source(allocator_type(state, 2));
    source.emplace(4);
    moved.splice(source); // moved has allocator id 2 after the swap.
    moved.sort();
    auto current = moved.begin();
    if (current != moved.end()) current = moved.erase(current);
    (void)current;
}

int main() { return 0; }
