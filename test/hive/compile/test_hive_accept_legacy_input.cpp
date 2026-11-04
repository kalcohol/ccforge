#include <hive>

#include <cstddef>
#include <iterator>
#include <type_traits>

struct legacy_proxy {
    int value;
    operator int() & { return value; }
};

struct legacy_input_iterator {
    using iterator_category = std::input_iterator_tag;
    using value_type = int;
    using difference_type = std::ptrdiff_t;
    using pointer = legacy_proxy*;
    using reference = legacy_proxy&;

    legacy_proxy* current = nullptr;

    legacy_input_iterator() = default;
    explicit legacy_input_iterator(legacy_proxy* value) : current(value) {}
    reference operator*() const { return *current; }
    pointer operator->() const { return current; }
    legacy_input_iterator& operator++() { ++current; return *this; }
    legacy_input_iterator operator++(int) { auto old = *this; ++*this; return old; }
    friend bool operator==(legacy_input_iterator left, legacy_input_iterator right) {
        return left.current == right.current;
    }
    friend bool operator!=(legacy_input_iterator left, legacy_input_iterator right) {
        return !(left == right);
    }
};

// This is the compatibility distinction required by [sequence.reqmts]:
// Cpp17InputIterator-style proxy access need not model the C++20 concept.
static_assert(!std::input_iterator<legacy_input_iterator>);

void instantiate_legacy_input() {
    legacy_proxy input[]{{2}, {4}, {6}};
    const legacy_input_iterator first(input);
    const legacy_input_iterator last(input + 3);
    std::hive<int> values(first, last);
    values.insert(first, last);
    values.assign(first, last);
    std::hive deduced(first, last);
    static_assert(std::is_same_v<decltype(deduced), std::hive<int>>);
}

int main() { return 0; }
