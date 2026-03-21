#include <simd>

struct int_generator {
    template<class Index>
    constexpr int operator()(Index) const noexcept {
        return static_cast<int>(Index::value + 1);
    }
};

int main() {
    std::simd::vec<float, 4> values(int_generator{});
    return static_cast<int>(values[0]);
}
