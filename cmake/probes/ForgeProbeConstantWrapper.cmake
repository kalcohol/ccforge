# std::constant_wrapper (P2781 + P3978 + P4206). submdspan's current C++26
# surface uses it directly, so it needs its own stand-aside guard. P4206 is a
# C++26 DR and bumps the feature-test macro to 202606L; older 202603L native
# implementations are partial and must still make Forge stand aside.
check_cxx_source_compiles("
    #include <utility>
    #include <type_traits>

    #if !defined(__cpp_lib_constant_wrapper) || __cpp_lib_constant_wrapper < 202606L
    #error incomplete constant_wrapper
    #endif

    constexpr int plus_one(int value) { return value + 1; }
    struct lookup {
        int values[2];
        constexpr int operator[](std::size_t index) const { return values[index]; }
    };

    int main() {
        using one = std::constant_wrapper<1zu>;
        static_assert(one::value == 1zu);
        static_assert(std::is_same_v<
            std::remove_cv_t<decltype(std::cw<2zu>)>,
            std::constant_wrapper<2zu>>);
        static_assert(std::is_same_v<
            decltype(std::cw<1> + std::integral_constant<int, 2>{}),
            std::constant_wrapper<3>>);
        static_assert(std::is_same_v<
            decltype(std::cw<&plus_one>(std::cw<2>)),
            std::constant_wrapper<3>>);
        static_assert(std::is_same_v<
            decltype(std::cw<lookup{{4, 5}}>[std::cw<1zu>]),
            std::constant_wrapper<5>>);
        static_assert(std::is_same_v<
            decltype(++std::cw<1>),
            std::constant_wrapper<2>>);
        return static_cast<int>(one{});
    }
" FORGE_CONSTANT_WRAPPER_FULL)
check_cxx_source_compiles("
    #include <utility>
    using probe = std::constant_wrapper<1zu>;
    int main() {
        probe* p = nullptr;
        (void)p;
        return 0;
    }
" FORGE_CONSTANT_WRAPPER_PARTIAL)
_forge_decide("std::constant_wrapper" CONSTANT_WRAPPER FORGE_CONSTANT_WRAPPER_FULL FORGE_CONSTANT_WRAPPER_PARTIAL)
