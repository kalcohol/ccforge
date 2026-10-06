#include <execution>

#include <cstddef>
#include <limits>
#include <type_traits>
#include <utility>

namespace ex = std::execution;
namespace scope_detail = ex::__forge_counting_scope;
using state = scope_detail::__scope_state;

template<class Scope>
constexpr bool limit_interface_is_valid() {
    static_assert(std::is_same_v<decltype(Scope::max_associations),
                                 const std::size_t>);
    static_assert(std::is_same_v<decltype(&Scope::max_associations),
                                 const std::size_t*>);
    using limit_constant =
        std::integral_constant<std::size_t, Scope::max_associations>;
    constexpr const std::size_t* address = &Scope::max_associations;
    static_assert(*address == limit_constant::value);
    static_assert(limit_constant::value ==
                  std::numeric_limits<std::size_t>::max());

    using token = typename Scope::token;
    using association = decltype(std::declval<const token&>().try_associate());
    static_assert(ex::scope_association<association>);
    static_assert(noexcept(std::declval<const token&>().try_associate()));
    static_assert(noexcept(std::declval<const association&>().try_associate()));
    static_assert(std::is_same_v<
        decltype(std::declval<const association&>().try_associate()), association>);
    return true;
}

constexpr bool association_boundaries_are_valid(std::size_t limit) {
    constexpr state states[]{
        state::unused, state::unused_and_closed, state::open,
        state::open_and_joining, state::closed, state::closed_and_joining,
        state::joined};
    for (state current : states) {
        const auto full = scope_detail::__scope_try_associate(current, limit, limit);
        if (full.__engaged || full.__count != limit || full.__state != current) {
            return false;
        }
        const auto zero = scope_detail::__scope_try_associate(current, 0, 0);
        if (zero.__engaged || zero.__count != 0 || zero.__state != current) {
            return false;
        }

        const bool accepts = current == state::unused || current == state::open ||
                             current == state::open_and_joining;
        const state expected = current == state::unused ? state::open : current;
        const auto first = scope_detail::__scope_try_associate(current, 0, limit);
        if (first.__engaged != accepts ||
            first.__count != (accepts ? 1u : 0u) ||
            first.__state != (accepts ? expected : current)) {
            return false;
        }
        const auto last =
            scope_detail::__scope_try_associate(current, limit - 1, limit);
        if (last.__engaged != accepts ||
            last.__count != (accepts ? limit : limit - 1) ||
            last.__state != (accepts ? expected : current)) {
            return false;
        }
    }
    return true;
}

static_assert(limit_interface_is_valid<ex::simple_counting_scope>());
static_assert(limit_interface_is_valid<ex::counting_scope>());
static_assert(noexcept(scope_detail::__scope_try_associate(state::unused, 0, 0)));
static_assert(association_boundaries_are_valid(
    ex::simple_counting_scope::max_associations));
static_assert(association_boundaries_are_valid(ex::counting_scope::max_associations));

int main() {}
