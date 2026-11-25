#pragma once

#include "awaitable_conformance_types.hpp"

#include <coroutine>
#include <memory>
#include <stdexcept>

namespace awaitable_tests {

struct source_guard {
    bool* destroyed;
    ~source_guard() { *destroyed = true; }
};
struct owned_int : int_awaiter {
    std::unique_ptr<source_guard> guard;
    explicit owned_int(bool* flag) : guard(new source_guard{flag}) {}
};
struct owned_void : void_awaiter {
    std::unique_ptr<source_guard> guard;
    explicit owned_void(bool* flag) : guard(new source_guard{flag}) {}
};
struct owned_error : owned_int {
    using owned_int::owned_int;
    int await_resume() const { throw std::runtime_error("await result"); }
};
struct owned_suspend_error : owned_int {
    using owned_int::owned_int;
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<>) const { throw std::runtime_error("await suspend"); }
};
struct owned_stopped : owned_int {
    using owned_int::owned_int;
    bool await_ready() const noexcept { return false; }
    template<class P>
    auto await_suspend(std::coroutine_handle<P> h) const noexcept -> std::coroutine_handle<> {
        return h.promise().unhandled_stopped();
    }
};
} // namespace awaitable_tests
