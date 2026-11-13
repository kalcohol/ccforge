#include <forge/execution_extensions.hpp>
#include <execution>

#include <exception>
#include <type_traits>
#include <utility>

struct multi_value_sender {
    using sender_concept = std::execution::sender_t;

    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<
            std::execution::set_value_t(int),
            std::execution::set_value_t(double)> {
        return {};
    }

    auto get_env() const noexcept -> forge::empty_env { return {}; }
};

template<class... Sigs>
struct signature_sender {
    using sender_concept = std::execution::sender_t;
    template<class Self, class Env>
    static auto get_completion_signatures() noexcept
        -> std::execution::completion_signatures<Sigs...> { return {}; }
    auto get_env() const noexcept -> forge::empty_env { return {}; }
};

struct throwing_copy {
    throwing_copy(const throwing_copy&) noexcept(false);
    throwing_copy(throwing_copy&&) noexcept;
};

template<class S>
using transferred = std::execution::completion_signatures_of_t<
    decltype(std::execution::continues_on(std::declval<S>(),
        std::execution::inline_scheduler{})), forge::empty_env>;

static_assert(std::same_as<transferred<signature_sender<
    std::execution::set_value_t(const int&), std::execution::set_error_t(int&),
    std::execution::set_stopped_t()>>, std::execution::completion_signatures<
    std::execution::set_value_t(int), std::execution::set_error_t(int),
    std::execution::set_stopped_t()>>);
static_assert(std::same_as<transferred<signature_sender<
    std::execution::set_value_t(const throwing_copy&)>>, std::execution::completion_signatures<
    std::execution::set_value_t(throwing_copy), std::execution::set_error_t(std::exception_ptr)>>);
static_assert(std::same_as<transferred<signature_sender<
    std::execution::set_error_t(const throwing_copy&)>>, std::execution::completion_signatures<
    std::execution::set_error_t(throwing_copy), std::execution::set_error_t(std::exception_ptr)>>);
// A throwing copy is irrelevant when the declared datum can be moved without throwing.
static_assert(std::same_as<transferred<signature_sender<
    std::execution::set_value_t(throwing_copy)>>, std::execution::completion_signatures<
    std::execution::set_value_t(throwing_copy)>>);
static_assert(std::same_as<transferred<signature_sender<
    std::execution::set_value_t(), std::execution::set_value_t(int),
    std::execution::set_value_t(int, double), std::execution::set_value_t(const int&)>>,
    std::execution::completion_signatures<std::execution::set_value_t(),
    std::execution::set_value_t(int), std::execution::set_value_t(int, double)>>);

struct error_scheduler;
struct schedule_attributes {
    auto query(std::execution::get_completion_scheduler_t<std::execution::set_value_t>)
        const noexcept -> error_scheduler;
};
struct error_schedule_sender : signature_sender<std::execution::set_value_t(),
    std::execution::set_error_t(long), std::execution::set_stopped_t()> {
    auto get_env() const noexcept -> schedule_attributes { return {}; }
};
struct error_scheduler {
    using scheduler_concept = std::execution::scheduler_t;
    auto schedule() const noexcept -> error_schedule_sender { return {}; }
    bool operator==(const error_scheduler&) const noexcept = default;
};
inline auto schedule_attributes::query(
    std::execution::get_completion_scheduler_t<std::execution::set_value_t>)
    const noexcept -> error_scheduler { return {}; }
static_assert(std::execution::scheduler<error_scheduler>);
using scheduled_signatures = std::execution::completion_signatures_of_t<
    decltype(std::execution::continues_on(multi_value_sender{}, error_scheduler{})),
    forge::empty_env>;
static_assert(std::same_as<scheduled_signatures, std::execution::completion_signatures<
    std::execution::set_value_t(int), std::execution::set_value_t(double),
    std::execution::set_error_t(long), std::execution::set_stopped_t()>>);

int main() {
    auto sender = std::execution::continues_on(
        multi_value_sender{}, std::execution::inline_scheduler{});
    using cs = std::execution::completion_signatures_of_t<decltype(sender), forge::empty_env>;
    static_assert(std::same_as<cs, std::execution::completion_signatures<
        std::execution::set_value_t(int), std::execution::set_value_t(double)>>);
}
