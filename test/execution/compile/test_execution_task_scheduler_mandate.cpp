#include <execution>

#include <concepts>
#include <exception>
#include <memory>
#include <type_traits>
#include <utility>

namespace ex = std::execution;

template<class... Extra>
struct scheduler {
    using scheduler_concept = ex::scheduler_tag;

    struct sender {
        using sender_concept = ex::sender_tag;

        template<class Self, class... Env>
        static consteval auto get_completion_signatures() noexcept {
            return ex::completion_signatures<ex::set_value_t(), Extra...>{};
        }

        template<ex::receiver R>
        auto connect(R rcvr) const {
            return ex::connect(ex::schedule(ex::inline_scheduler{}), std::move(rcvr));
        }
    };

    sender schedule() const noexcept { return {}; }
    auto query(ex::get_forward_progress_guarantee_t) const noexcept {
        return ex::forward_progress_guarantee::weakly_parallel;
    }
    bool operator==(const scheduler&) const noexcept = default;
};

using infallible_scheduler = scheduler<>;
using error_scheduler = scheduler<ex::set_error_t(std::exception_ptr)>;
using stopped_scheduler = scheduler<ex::set_stopped_t()>;

template<class Sch>
using advertised = ex::completion_signatures_of_t<ex::schedule_result_t<Sch>, ex::env<>>;

static_assert(std::same_as<advertised<infallible_scheduler>, ex::completion_signatures<ex::set_value_t()>>);
static_assert(std::same_as<advertised<error_scheduler>,
                          ex::completion_signatures<ex::set_value_t(), ex::set_error_t(std::exception_ptr)>>);
static_assert(std::same_as<advertised<stopped_scheduler>,
                          ex::completion_signatures<ex::set_value_t(), ex::set_stopped_t()>>);

#if defined(FORGE_TASK_SCHEDULER_ERROR)
using selected_scheduler = error_scheduler;
#elif defined(FORGE_TASK_SCHEDULER_STOPPED)
using selected_scheduler = stopped_scheduler;
#else
using selected_scheduler = infallible_scheduler;
#endif

static_assert(ex::scheduler<ex::task_scheduler>);
static_assert(std::is_constructible_v<ex::task_scheduler, selected_scheduler>);
static_assert(!std::is_convertible_v<selected_scheduler, ex::task_scheduler>);
static_assert(!std::is_default_constructible_v<ex::task_scheduler>);
static_assert(!std::is_constructible_v<ex::task_scheduler, int>);
static_assert(!std::is_constructible_v<ex::task_scheduler, ex::task_scheduler, std::allocator<int>>);

struct inherited_scheduler : ex::task_scheduler {
    using ex::task_scheduler::task_scheduler;
};

// GCC 16 checked the inherited constructor's scheduler constraint while
// checking the derived scheduler's own copyability.
static_assert(ex::scheduler<inherited_scheduler>);
static_assert(ex::scheduler<inherited_scheduler&>);
static_assert(std::is_constructible_v<ex::task_scheduler, inherited_scheduler&>);
static_assert(std::is_constructible_v<ex::task_scheduler, inherited_scheduler&, std::allocator<int>>);

int main() {
    // A type-trait alone does not instantiate the constructor's mandate.
    [[maybe_unused]] ex::task_scheduler erased{selected_scheduler{}};
}
