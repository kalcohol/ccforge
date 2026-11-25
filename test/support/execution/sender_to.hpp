#pragma once

#include <concepts>
#include <execution>
#include <type_traits>
#include <utility>

namespace test_execution {

template<class Signature, class R>
concept accepts_completion = requires(Signature* signature) {
    []<class Tag, class... Args>(Tag (*)(Args...))
        requires requires {
            std::declval<Tag>()(std::declval<std::remove_cvref_t<R>>(), std::declval<Args>()...);
        }
    {}(signature);
};

template<class R, class Completions>
concept accepts_completions = requires(Completions* completions) {
    []<accepts_completion<R>... Signatures>(std::execution::completion_signatures<Signatures...>*)
    {}(completions);
};

// Check the advertised completions as well as connect using only standard CPOs.
template<class S, class R>
concept sender_to = std::execution::receiver<R> &&
    std::execution::sender_in<S, std::execution::env_of_t<R>> &&
    accepts_completions<R, std::execution::completion_signatures_of_t<S, std::execution::env_of_t<R>>> &&
    requires(S&& source, R&& receiver) {
        { std::execution::connect(std::forward<S>(source), std::forward<R>(receiver)) }
            -> std::execution::operation_state;
    };

} // namespace test_execution
