#include <execution>

struct throwing_stopped_receiver {
    using receiver_concept = std::execution::receiver_t;

    void set_stopped() && {}
};

int main() {
    std::execution::set_stopped(throwing_stopped_receiver{});
}
