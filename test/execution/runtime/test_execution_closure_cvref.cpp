#include <gtest/gtest.h>

#include <execution>
#include <array>
#include <memory>
#include <tuple>
#include <utility>

namespace {

namespace ex = std::execution;

enum class category { lvalue, const_lvalue, rvalue, const_rvalue };

struct capture_state {
    std::array<int, 4> constructions{};
    int calls = 0;
};

template<bool ReturnsSender>
struct tracked_callback {
    capture_state* state;

    explicit tracked_callback(capture_state& s) noexcept : state(&s) {}
    tracked_callback(tracked_callback& other) noexcept : state(other.state) {
        ++state->constructions[0];
    }
    tracked_callback(const tracked_callback& other) noexcept : state(other.state) {
        ++state->constructions[1];
    }
    tracked_callback(tracked_callback&& other) noexcept : state(other.state) {
        ++state->constructions[2];
    }
    tracked_callback(const tracked_callback&& other) noexcept : state(other.state) {
        ++state->constructions[3];
    }

    template<class... Args>
    auto operator()(Args&&...) noexcept {
        ++state->calls;
        if constexpr (ReturnsSender) {
            return ex::just(42);
        } else {
            return 42;
        }
    }
};

template<bool ReturnsSender>
struct owned_callback {
    std::unique_ptr<int> value;

    explicit owned_callback(int v) : value(std::make_unique<int>(v)) {}
    owned_callback(owned_callback&&) noexcept = default;
    owned_callback(const owned_callback&) = delete;

    template<class... Args>
    auto operator()(Args&&...) && noexcept {
        if constexpr (ReturnsSender) {
            return ex::just(*value);
        } else {
            return *value;
        }
    }
};

template<bool ReturnsSender>
struct mutable_copy_callback {
    int value;

    explicit mutable_copy_callback(int v) noexcept : value(v) {}
    mutable_copy_callback(mutable_copy_callback&&) noexcept = default;
    mutable_copy_callback(mutable_copy_callback& other) noexcept : value(other.value) {}
    mutable_copy_callback(const mutable_copy_callback&) = delete;

    template<class... Args>
    auto operator()(Args&&...) && noexcept {
        if constexpr (ReturnsSender) {
            return ex::just(value);
        } else {
            return value;
        }
    }
};

template<bool ReturnsSender>
struct const_move_callback {
    int value;

    explicit const_move_callback(int v) noexcept : value(v) {}
    const_move_callback(const_move_callback&&) noexcept = default;
    const_move_callback(const const_move_callback&& other) noexcept : value(other.value) {}
    const_move_callback(const const_move_callback&) = delete;

    template<class... Args>
    auto operator()(Args&&...) && noexcept {
        if constexpr (ReturnsSender) {
            return ex::just(value);
        } else {
            return value;
        }
    }
};

template<bool Pipe, class Closure, class Source>
auto apply_closure(Closure&& closure, Source&& source) {
    if constexpr (Pipe) {
        return std::forward<Source>(source) | std::forward<Closure>(closure);
    } else {
        return std::forward<Closure>(closure)(std::forward<Source>(source));
    }
}

template<category Category, bool Pipe, class Closure, class Source>
auto apply_category(Closure& closure, Source&& source) {
    if constexpr (Category == category::lvalue) {
        return apply_closure<Pipe>(closure, std::forward<Source>(source));
    } else if constexpr (Category == category::const_lvalue) {
        return apply_closure<Pipe>(std::as_const(closure), std::forward<Source>(source));
    } else if constexpr (Category == category::rvalue) {
        return apply_closure<Pipe>(std::move(closure), std::forward<Source>(source));
    } else {
        return apply_closure<Pipe>(std::move(std::as_const(closure)), std::forward<Source>(source));
    }
}

template<category Category, bool Pipe, class MakeClosure, class MakeSource>
void check_category(MakeClosure make_closure, MakeSource make_source,
                    int expected_calls, int expected_value) {
    capture_state state;
    auto closure = make_closure(state);
    state.constructions = {};
    auto sender = apply_category<Category, Pipe>(closure, make_source());

    // Check application before connect performs its own legitimate capture moves.
    for (std::size_t i = 0; i != state.constructions.size(); ++i) {
        EXPECT_EQ(state.constructions[i], i == static_cast<std::size_t>(Category) ? 1 : 0);
    }
    EXPECT_EQ(state.calls, 0);

    auto result = ex::sync_wait(std::move(sender));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), expected_value);
    EXPECT_EQ(state.calls, expected_calls);
}

template<class MakeClosure, class MakeSource>
void check_all_categories(MakeClosure make_closure, MakeSource make_source,
                         int expected_calls = 1, int expected_value = 42) {
    check_category<category::lvalue, false>(make_closure, make_source, expected_calls, expected_value);
    check_category<category::const_lvalue, false>(make_closure, make_source, expected_calls, expected_value);
    check_category<category::rvalue, false>(make_closure, make_source, expected_calls, expected_value);
    check_category<category::const_rvalue, false>(make_closure, make_source, expected_calls, expected_value);
    check_category<category::lvalue, true>(make_closure, make_source, expected_calls, expected_value);
    check_category<category::const_lvalue, true>(make_closure, make_source, expected_calls, expected_value);
    check_category<category::rvalue, true>(make_closure, make_source, expected_calls, expected_value);
    check_category<category::const_rvalue, true>(make_closure, make_source, expected_calls, expected_value);
}

template<class Sender>
void expect_value(Sender sender, int expected = 42) {
    auto result = ex::sync_wait(std::move(sender));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), expected);
}

} // namespace

TEST(ExecutionClosureCvref, ThenForwardsAllCaptureCategories) {
    check_all_categories([](capture_state& s) { return ex::then(tracked_callback<false>{s}); },
        [] { return ex::just(7); });
}

TEST(ExecutionClosureCvref, UponForwardsAllCaptureCategories) {
    check_all_categories([](capture_state& s) { return ex::upon_error(tracked_callback<false>{s}); },
        [] { return ex::just_error(7); });
    check_all_categories([](capture_state& s) { return ex::upon_stopped(tracked_callback<false>{s}); },
        [] { return ex::just_stopped(); });
}

TEST(ExecutionClosureCvref, LetForwardsAllCaptureCategories) {
    check_all_categories([](capture_state& s) { return ex::let_value(tracked_callback<true>{s}); },
        [] { return ex::just(7); });
    check_all_categories([](capture_state& s) { return ex::let_error(tracked_callback<true>{s}); },
        [] { return ex::just_error(7); });
    check_all_categories([](capture_state& s) { return ex::let_stopped(tracked_callback<true>{s}); },
        [] { return ex::just_stopped(); });
}

TEST(ExecutionClosureCvref, BulkForwardsAllCopyableCaptureCategories) {
    check_all_categories([](capture_state& s) { return ex::bulk(3, tracked_callback<false>{s}); },
        [] { return ex::just(7); }, 3, 7);
    check_all_categories([](capture_state& s) { return ex::bulk_unchunked(3, tracked_callback<false>{s}); },
        [] { return ex::just(7); }, 3, 7);
    check_all_categories([](capture_state& s) { return ex::bulk_chunked(3, tracked_callback<false>{s}); },
        [] { return ex::just(7); }, 1, 7);
}

TEST(ExecutionClosureCvref, MoveOnlyCapturesAreTransferredFromRvalueClosures) {
    expect_value(ex::then(owned_callback<false>{42})(ex::just(7)));
    expect_value(ex::just(7) | ex::then(owned_callback<false>{42}));
    expect_value(ex::upon_error(owned_callback<false>{42})(ex::just_error(7)));
    expect_value(ex::just_error(7) | ex::upon_error(owned_callback<false>{42}));
    expect_value(ex::upon_stopped(owned_callback<false>{42})(ex::just_stopped()));
    expect_value(ex::just_stopped() | ex::upon_stopped(owned_callback<false>{42}));
    expect_value(ex::let_value(owned_callback<true>{42})(ex::just(7)));
    expect_value(ex::just(7) | ex::let_value(owned_callback<true>{42}));
    expect_value(ex::let_error(owned_callback<true>{42})(ex::just_error(7)));
    expect_value(ex::just_error(7) | ex::let_error(owned_callback<true>{42}));
    expect_value(ex::let_stopped(owned_callback<true>{42})(ex::just_stopped()));
    expect_value(ex::just_stopped() | ex::let_stopped(owned_callback<true>{42}));
}

TEST(ExecutionClosureCvref, MutableCopyCapturesRemainReusable) {
    auto closure = ex::then(mutable_copy_callback<false>{42});
    expect_value(closure(ex::just(7)));
    expect_value(ex::just(7) | closure);

    auto let_closure = ex::let_value(mutable_copy_callback<true>{42});
    expect_value(let_closure(ex::just(7)));
    expect_value(ex::just(7) | let_closure);
}

TEST(ExecutionClosureCvref, ConstRvalueCapturesDoNotFallBackToConstLvalue) {
    const auto closure = ex::then(const_move_callback<false>{42});
    expect_value(std::move(closure)(ex::just(7)));
    expect_value(ex::just(7) | std::move(closure));

    const auto let_closure = ex::let_value(const_move_callback<true>{42});
    expect_value(std::move(let_closure)(ex::just(7)));
    expect_value(ex::just(7) | std::move(let_closure));
}

TEST(ExecutionClosureCvref, CopyableBulkLvalueClosureCanBeAppliedRepeatedly) {
    capture_state state;
    auto closure = ex::bulk(3, tracked_callback<false>{state});
    expect_value(closure(ex::just(7)), 7);
    expect_value(ex::just(7) | closure, 7);
    EXPECT_EQ(state.calls, 6);
}
