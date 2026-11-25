#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <execution>
#include <new>
#include <optional>
#include <type_traits>
#include <utility>

namespace static_opstate_scope_fixture {
namespace ex = std::execution;

struct allocation_counts {
    int allocations = 0;
    int deallocations = 0;
    int attempts = 0;
    int fail_on = 0;
};

// malloc-backed, including over-aligned rebinds: this allocator never calls new.
template<class T>
struct counting_allocator {
    using value_type = T;
    allocation_counts* counts;
    explicit counting_allocator(allocation_counts* c) noexcept : counts(c) {}
    template<class U>
    counting_allocator(const counting_allocator<U>& a) noexcept : counts(a.counts) {}
    T* allocate(std::size_t n) {
        if (++counts->attempts == counts->fail_on) throw std::bad_alloc{};
        const auto alignment = (alignof(T) > alignof(void*) ? alignof(T) : alignof(void*));
        void* raw = std::malloc(n * sizeof(T) + alignment - 1 + sizeof(void*));
        if (!raw) throw std::bad_alloc{};
        auto addr = reinterpret_cast<std::uintptr_t>(raw) + sizeof(void*);
        addr = (addr + alignment - 1) & ~(alignment - 1);
        auto* p = reinterpret_cast<T*>(addr);
        reinterpret_cast<void**>(p)[-1] = raw;
        ++counts->allocations;
        return p;
    }
    void deallocate(T* p, std::size_t) noexcept {
        ++counts->deallocations;
        std::free(reinterpret_cast<void**>(p)[-1]);
    }
    template<class U>
    bool operator==(const counting_allocator<U>& a) const noexcept { return counts == a.counts; }
};

inline auto allocator_env(allocation_counts& counts) {
    return ex::prop(std::get_allocator, counting_allocator<std::byte>{&counts});
}

struct observations {
    int connects = 0;
    int starts = 0;
    int live = 0;
    int values = 0;
    int stopped = 0;
    bool aligned = true;
    bool fail_connect = false;
    bool deferred = false;
    void* pending = nullptr;
    void (*finish)(void*) noexcept = nullptr;
};
struct connect_error {};

template<std::size_t Bytes, std::size_t Alignment>
struct padded_sender {
    using sender_concept = ex::sender_tag;
    using completion_signatures = ex::completion_signatures<ex::set_value_t(), ex::set_stopped_t()>;
    template<class Self, class Env>
    static auto get_completion_signatures() noexcept -> completion_signatures { return {}; }
    observations* observed;
    auto get_env() const noexcept -> ex::env<> { return {}; }

    template<class R>
    struct alignas(Alignment) op {
        using operation_state_concept = ex::operation_state_tag;
        R receiver;
        observations* observed;
        std::array<std::byte, Bytes> padding;
        struct stop_fn {
            op* self;
            void operator()() const noexcept { self->complete_stopped(); }
        };
        using token_t = std::remove_cvref_t<decltype(
            std::get_stop_token(ex::get_env(std::declval<const R&>())))>;
        std::optional<std::stop_callback_for_t<token_t, stop_fn>> callback;
        op(R r, observations* o) : receiver(std::move(r)), observed(o) { ++observed->live; }
        op(const op&) = delete;
        op(op&&) = delete;
        ~op() { --observed->live; }
        void complete_stopped() noexcept {
            callback.reset();
            ex::set_stopped(std::move(receiver));
        }
        void start() & noexcept {
            ++observed->starts;
            observed->aligned &= reinterpret_cast<std::uintptr_t>(this) % Alignment == 0;
            if (observed->deferred) {
                auto token = std::get_stop_token(ex::get_env(receiver));
                if (token.stop_requested()) { complete_stopped(); return; }
                observed->pending = this;
                observed->finish = [](void* p) noexcept {
                    auto* self = static_cast<op*>(p);
                    self->callback.reset();
                    ex::set_value(std::move(self->receiver));
                };
                callback.emplace(token, stop_fn{this});
            } else {
                ex::set_value(std::move(receiver));
            }
        }
    };
    template<class R>
    auto connect(R r) const -> op<R> {
        ++observed->connects;
        if (observed->fail_connect) throw connect_error{};
        return op<R>{std::move(r), observed};
    }
};

using large_sender = padded_sender<8192, 64>;

template<class Token = std::never_stop_token>
struct receiver {
    using receiver_concept = ex::receiver_tag;
    observations* observed;
    Token token{};
    void set_value() && noexcept { ++observed->values; }
    template<class E> void set_error(E&&) && noexcept { std::terminate(); }
    void set_stopped() && noexcept { ++observed->stopped; }
    auto get_env() const noexcept {
        return ex::env{ex::prop(std::get_stop_token, token),
            ex::prop(ex::get_start_scheduler, ex::inline_scheduler{})};
    }
};

} // namespace static_opstate_scope_fixture
