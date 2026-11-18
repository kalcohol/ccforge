#pragma once

#include <execution>

#include <concepts>
#include <utility>

namespace scheduler_fixture {
namespace ex = std::execution;

template<class Scheduler>
struct schedule_sender {
    using sender_concept = ex::sender_tag;
    Scheduler scheduler;

    constexpr auto get_env() const noexcept -> Scheduler { return scheduler; }

    template<class Self, class... Env>
    static constexpr auto get_completion_signatures() noexcept
        -> ex::completion_signatures<ex::set_value_t()> { return {}; }

    template<ex::receiver R>
    auto connect(R receiver) const
        noexcept(noexcept(ex::connect(ex::just(), std::move(receiver))))
        -> ex::connect_result_t<decltype(ex::just()), R> {
        return ex::connect(ex::just(), std::move(receiver));
    }
};

// [exec.sched]/5-6 compares the scheduler to the environment of schedule(sch).
template<class Query, class Scheduler, class Attrs, class... Envs>
constexpr bool agrees(Query query, const Scheduler& scheduler, const Attrs& attrs,
                      const Envs&... envs) {
    if constexpr (requires { query(scheduler, envs...); }) {
        if constexpr (requires { query(attrs, envs...); }) {
            using result = decltype(query(scheduler, envs...));
            if constexpr (!std::same_as<result, decltype(query(attrs, envs...))>) {
                return false;
            } else if constexpr (ex::scheduler<result>) {
                return query(scheduler, envs...) == query(attrs, envs...);
            } else {
                return true;
            }
        } else {
            return false;
        }
    } else {
        return !requires { query(attrs, envs...); };
    }
}

template<class Tag, class Scheduler, class Attrs, class... Envs>
constexpr bool agrees_for(const Scheduler& scheduler, const Attrs& attrs, const Envs&... envs) {
    return agrees(ex::get_completion_scheduler<Tag>, scheduler, attrs, envs...) &&
           agrees(ex::get_completion_domain<Tag>, scheduler, attrs, envs...);
}

template<class Tag, class Signature>
inline constexpr bool has_tag = false;

template<class Tag, class... Args>
inline constexpr bool has_tag<Tag, Tag(Args...)> = true;

template<class Tag, class... Sigs, class Scheduler, class Attrs, class... Envs>
constexpr bool agrees_for_completion(ex::completion_signatures<Sigs...>,
    const Scheduler& scheduler, const Attrs& attrs, const Envs&... envs) {
    // [exec.get.compl.sched]/6 rules out querying an absent completion tag.
    if constexpr ((has_tag<Tag, Sigs> || ...)) {
        if constexpr (std::same_as<Tag, ex::set_value_t>) {
            if constexpr (sizeof...(Envs) == 0 &&
                          requires { ex::get_completion_scheduler<Tag>(attrs); }) {
                if (!(ex::get_completion_scheduler<Tag>(attrs) == scheduler)) return false;
            }
            if (!agrees(ex::get_completion_domain<>, scheduler, attrs, envs...)) return false;
        }
        return agrees_for<Tag>(scheduler, attrs, envs...);
    }
    return true;
}

template<class Scheduler, class... Envs>
constexpr bool agrees_with_schedule(const Scheduler& scheduler, const Envs&... envs) {
    auto sender = ex::schedule(scheduler);
    decltype(auto) attrs = ex::get_env(sender);
    using signatures = ex::completion_signatures_of_t<decltype(sender), decltype((envs))...>;
    return agrees_for_completion<ex::set_value_t>(signatures{}, scheduler, attrs, envs...) &&
           agrees_for_completion<ex::set_error_t>(signatures{}, scheduler, attrs, envs...) &&
           agrees_for_completion<ex::set_stopped_t>(signatures{}, scheduler, attrs, envs...);
}
} // namespace scheduler_fixture
