#include <execution>

#include <exception>
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

    auto get_env() const noexcept -> std::execution::empty_env { return {}; }
};

int main() {
    auto sender = std::execution::continues_on(
        multi_value_sender{}, std::execution::inline_scheduler{});
    (void)std::execution::get_completion_signatures(
        sender, std::execution::empty_env{});
}
