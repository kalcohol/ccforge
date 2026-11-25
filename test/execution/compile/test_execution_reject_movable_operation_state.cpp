#include <exception>
#include <execution>
#include <utility>

struct receiver {
    using receiver_concept = std::execution::receiver_tag;

    void set_value() && noexcept {}
    void set_error(std::exception_ptr) && noexcept {}
    void set_stopped() && noexcept {}

    auto get_env() const noexcept -> std::execution::env<> {
        return {};
    }
};

int main() {
    auto op = std::execution::connect(std::execution::just(), receiver{});
    auto moved = std::move(op);
    (void)moved;
}
