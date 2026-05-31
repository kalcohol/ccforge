#pragma once

#include <new>

namespace forge_test {

struct destroy_context_base {
    virtual ~destroy_context_base() = default;
    virtual void destroy() noexcept = 0;
};

template<class Op>
struct operation_destroy_context final : destroy_context_base {
    bool* called;
    alignas(Op) unsigned char storage[sizeof(Op)]{};
    bool has_value = false;

    explicit operation_destroy_context(bool* c) : called(c) {}

    ~operation_destroy_context() override {
        reset();
    }

    template<class Factory>
    auto emplace_from(Factory&& factory) -> Op& {
        ::new (static_cast<void*>(storage)) Op(static_cast<Factory&&>(factory)());
        has_value = true;
        return get();
    }

    [[nodiscard]] auto get() noexcept -> Op& {
        return *std::launder(reinterpret_cast<Op*>(storage));
    }

    void destroy() noexcept override {
        *called = true;
        reset();
    }

    void reset() noexcept {
        if (!has_value) {
            return;
        }
        get().~Op();
        has_value = false;
    }
};

} // namespace forge_test
