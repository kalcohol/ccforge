#include <execution>

struct throwing_receiver {
    using receiver_concept = std::execution::receiver_tag;

    void set_value() && {}
};

int main() {
    std::execution::set_value(throwing_receiver{});
}
