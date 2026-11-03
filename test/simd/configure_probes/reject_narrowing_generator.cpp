#include <simd>

struct int_generator {
    template<class Index>
#if defined(FORGE_SIMD_POSITIVE_CONTROL)
    constexpr float operator()(Index) const noexcept {
        return static_cast<float>(Index::value + 1);
    }
#else
    constexpr int operator()(Index) const noexcept {
        return static_cast<int>(Index::value + 1);
    }
#endif
};

int main() {
    std::simd::vec<float, 4> values(int_generator{});
    return static_cast<int>(values[0]);
}
