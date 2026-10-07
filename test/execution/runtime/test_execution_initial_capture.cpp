#include <gtest/gtest.h>

#include "../initial_capture_probe.hpp"

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace {

using namespace initial_capture_probe;

template<kind Kind, bool Full, category Category>
void check_category() {
    capture_state state;
    tracked_callback<is_let<Kind>> fn{state};
    auto holder = make<Kind, Full>(as_category<Category>(fn));
    static_assert(std::same_as<std::remove_cvref_t<decltype(captured(holder))>, decltype(fn)>);
    for (std::size_t i = 0; i != state.constructions.size(); ++i) {
        EXPECT_EQ(state.constructions[i], i == static_cast<std::size_t>(Category) ? 1 : 0);
    }
    EXPECT_EQ(state.calls, 0);
    EXPECT_NE(std::addressof(captured(holder)), std::addressof(fn));
    fn.value = 99;
    EXPECT_EQ(captured(holder).value, 17);
}

template<kind Kind, bool Full>
void check_categories() {
    check_category<Kind, Full, category::lvalue>();
    check_category<Kind, Full, category::const_lvalue>();
    check_category<Kind, Full, category::rvalue>();
    check_category<Kind, Full, category::const_rvalue>();
}

template<kind Kind, bool Full, category Category>
void check_throwing_category() {
    capture_state state;
    tracked_callback<is_let<Kind>> fn{state};
    state.throw_on[static_cast<std::size_t>(Category)] = true;
    try {
        (void)make<Kind, Full>(as_category<Category>(fn));
        ADD_FAILURE() << "Capture constructor did not propagate its exception";
    } catch (const capture_failure& failure) {
        EXPECT_EQ(failure.selected, Category);
    } catch (...) {
        ADD_FAILURE() << "Capture constructor propagated a different exception";
    }
    for (std::size_t i = 0; i != state.constructions.size(); ++i) {
        EXPECT_EQ(state.constructions[i], i == static_cast<std::size_t>(Category) ? 1 : 0);
    }
    EXPECT_EQ(state.calls, 0);
}

template<kind Kind, bool Full>
void check_throwing_categories() {
    check_throwing_category<Kind, Full, category::lvalue>();
    check_throwing_category<Kind, Full, category::const_lvalue>();
    check_throwing_category<Kind, Full, category::rvalue>();
    check_throwing_category<Kind, Full, category::const_rvalue>();
}

template<kind Kind, bool Full>
void check_explicit_copy() {
    capture_state state;
    const explicit_copy_callback<is_let<Kind>> fn{state};
    auto holder = make<Kind, Full>(fn);
    EXPECT_EQ(state.constructions, (std::array<int, 4>{0, 1, 0, 0}));
    EXPECT_EQ(state.calls, 0);
    EXPECT_NE(std::addressof(captured(holder)), std::addressof(fn));
    EXPECT_EQ(captured(holder).value, 17);
}

template<kind Kind, bool Full>
void check_reference_wrapper() {
    capture_state state;
    move_only_callback<is_let<Kind>> fn{state};
    auto holder = make<Kind, Full>(std::ref(fn));
    static_assert(std::same_as<std::remove_cvref_t<decltype(captured(holder))>,
        std::reference_wrapper<decltype(fn)>>);
    EXPECT_EQ(std::addressof(captured(holder).get()), std::addressof(fn));
    EXPECT_EQ(state.constructions, (std::array<int, 4>{}));
    EXPECT_EQ(state.calls, 0);
    (void)std::invoke(captured(holder));
    EXPECT_EQ(state.calls, 1);
}

template<kind Kind, category Category>
void check_source_category() {
    capture_state state;
    tracked_source source{state};
    callback_body<is_let<Kind>> fn;
    auto holder = [&] {
        if constexpr (is_bulk<Kind>) return adaptor<Kind>(as_category<Category>(source), 3, fn);
        else return adaptor<Kind>(as_category<Category>(source), fn);
    }();
    for (std::size_t i = 0; i != state.constructions.size(); ++i) {
        EXPECT_EQ(state.constructions[i], i == static_cast<std::size_t>(Category) ? 1 : 0);
    }
    EXPECT_EQ(state.calls, 0);
    EXPECT_NE(std::addressof(captured_source(holder)), std::addressof(source));
    source.value = 99;
    EXPECT_EQ(captured_source(holder).value, 7);
}

template<kind Kind>
void check_source_categories() {
    check_source_category<Kind, category::lvalue>();
    check_source_category<Kind, category::const_lvalue>();
    check_source_category<Kind, category::rvalue>();
    check_source_category<Kind, category::const_rvalue>();
}

template<class F>
void all_kinds(F&& f) {
    f.template operator()<kind::then>();
    f.template operator()<kind::upon_error>();
    f.template operator()<kind::upon_stopped>();
    f.template operator()<kind::let_value>();
    f.template operator()<kind::let_error>();
    f.template operator()<kind::let_stopped>();
    f.template operator()<kind::bulk>();
    f.template operator()<kind::bulk_unchunked>();
    f.template operator()<kind::bulk_chunked>();
}

} // namespace

TEST(ExecutionInitialCapture, FullFactoriesOwnExactInputCvref) {
    all_kinds([]<kind Kind> { check_categories<Kind, true>(); });
}

TEST(ExecutionInitialCapture, PartialFactoriesOwnExactInputCvref) {
    all_kinds([]<kind Kind> { check_categories<Kind, false>(); });
}

TEST(ExecutionInitialCapture, ExplicitCopyIsDirectlyInitialized) {
    all_kinds([]<kind Kind> {
        if constexpr (!is_bulk<Kind>) {
            check_explicit_copy<Kind, true>();
            check_explicit_copy<Kind, false>();
        }
    });
}

TEST(ExecutionInitialCapture, FullFactoryConstructionExceptionsPropagate) {
    all_kinds([]<kind Kind> { check_throwing_categories<Kind, true>(); });
}

TEST(ExecutionInitialCapture, PartialFactoryConstructionExceptionsPropagate) {
    all_kinds([]<kind Kind> { check_throwing_categories<Kind, false>(); });
}

TEST(ExecutionInitialCapture, ReferenceWrappersDoNotCopyOrMoveTheirTarget) {
    all_kinds([]<kind Kind> {
        check_reference_wrapper<Kind, true>();
        check_reference_wrapper<Kind, false>();
    });
}

TEST(ExecutionInitialCapture, FullFactoriesDirectlyInitializeTheirSource) {
    all_kinds([]<kind Kind> { check_source_categories<Kind>(); });
}
