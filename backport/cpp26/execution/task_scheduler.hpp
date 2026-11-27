// MIT License
//
// Copyright (c) 2026 CC Forge Project
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include <cstddef>
#include <exception>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>

#include "affine.hpp"
#include "inline_scheduler.hpp"
#include "parallel_scheduler_replacement.hpp"

namespace std::execution {

namespace __forge_task_scheduler {

namespace __spi = parallel_scheduler_replacement;
namespace __stop = __forge_parsched_detail;

// The child receiver must not depend on the type of its own operation state.
class __proxy_base : public __spi::receiver_proxy {
    void* __inline_child_ = nullptr;
    void (*__destroy_inline_)(void*) noexcept = nullptr;
    std::shared_ptr<void> __overflow_child_;

protected:
    std::inplace_stop_token __token_{};

    bool __query_env(std::inplace_stop_token& __token) const noexcept final {
        __token = __token_;
        return true;
    }

public:
    const bool __cancellable;

    explicit __proxy_base(bool __can_stop) noexcept : __cancellable(__can_stop) {}

    template<class Child>
    void __keep_inline(Child* __child) noexcept {
        __inline_child_ = __child;
        __destroy_inline_ = [](void* __ptr) noexcept {
            std::destroy_at(static_cast<Child*>(__ptr));
        };
    }

    void __keep_overflow(std::shared_ptr<void> __child) noexcept {
        __overflow_child_ = std::move(__child);
    }

    void __destroy_child() noexcept {
        if (auto* __child = std::exchange(__inline_child_, nullptr)) __destroy_inline_(__child);
        __overflow_child_.reset();
    }
};

template<bool Cancellable>
struct __wrap_receiver {
    using receiver_concept = receiver_tag;
    __proxy_base* __proxy;

    auto get_env() const noexcept {
        if constexpr (Cancellable) {
            return prop{std::get_stop_token,
                        *__proxy->try_query<std::inplace_stop_token>(std::get_stop_token)};
        } else {
            return env<>{};
        }
    }

    void set_value() && noexcept {
        auto* __target = __proxy;
        // This ends the wrapped operation (and *this*) before the outward
        // receiver can destroy the entire enclosing operation.
        __target->__destroy_child();
        __target->set_value();
    }

    void set_stopped() && noexcept {
        auto* __target = __proxy;
        __target->__destroy_child();
        __target->set_stopped();
    }

    void set_error(std::exception_ptr __error) && noexcept {
        auto* __target = __proxy;
        __target->__destroy_child();
        __target->set_error(std::move(__error));
    }
};

template<class Sch>
inline constexpr unsigned char __type_key = 0;

struct __backend : __spi::parallel_scheduler_backend {
    virtual const void* __type() const noexcept = 0;
    virtual const void* __scheduler_address() const noexcept = 0;
    virtual bool __equals(const void*) const noexcept = 0;
    virtual forward_progress_guarantee __progress() noexcept = 0;
    virtual std::shared_ptr<__stop::__proxy_stop_cell> __make_stop_cell() const = 0;
};

template<class Sch>
concept __usable_stoppable_schedule =
    __forge_affine::__infallible_scheduler<Sch&, env_of_t<__wrap_receiver<true>>> &&
    __forge_sender_to<schedule_result_t<Sch&>, __wrap_receiver<true>>;

template<class Sch, bool Cancellable>
struct __child {
    using __receiver = __wrap_receiver<Cancellable>;
    using __sender = decltype(std::execution::schedule(std::declval<Sch&>()));
    connect_result_t<__sender, __receiver> __operation;

    explicit __child(Sch& __scheduler, __proxy_base& __proxy)
        : __operation(std::execution::connect(
              std::execution::schedule(__scheduler), __receiver{&__proxy})) {}
};

template<class Sch, class Allocator>
class __backend_for final : public __backend {
    [[no_unique_address]] Sch __scheduler_;
    [[no_unique_address]] Allocator __allocator_;

    template<bool Cancellable>
    void __start_child(__proxy_base& __proxy, std::span<std::byte> __storage) {
        using __node = __child<Sch, Cancellable>;
        auto* __address = static_cast<void*>(__storage.data());
        auto __size = __storage.size();
        __node* __node_ptr;
        if (__size >= sizeof(__node) && std::align(alignof(__node), sizeof(__node), __address, __size)) {
            __node_ptr = ::new (__address) __node(__scheduler_, __proxy);
            __proxy.__keep_inline(__node_ptr);
        } else {
            auto __allocated = std::allocate_shared<__node>(__allocator_, __scheduler_, __proxy);
            __node_ptr = __allocated.get();
            __proxy.__keep_overflow(std::move(__allocated));
        }
        std::execution::start(__node_ptr->__operation);
    }

public:
    template<class Source>
    explicit __backend_for(Source&& __scheduler, const Allocator& __alloc)
        : __scheduler_(std::forward<Source>(__scheduler)), __allocator_(__alloc) {}

    const void* __type() const noexcept override { return &__type_key<Sch>; }
    const void* __scheduler_address() const noexcept override { return std::addressof(__scheduler_); }
    bool __equals(const void* __rhs) const noexcept override {
        return __scheduler_ == *static_cast<const Sch*>(__rhs);
    }

    forward_progress_guarantee __progress() noexcept override {
        return std::execution::get_forward_progress_guarantee(__scheduler_);
    }

    std::shared_ptr<__stop::__proxy_stop_cell> __make_stop_cell() const override {
        return std::allocate_shared<__stop::__proxy_stop_cell>(__allocator_);
    }

    void schedule(__spi::receiver_proxy& __receiver, std::span<std::byte> __storage) noexcept override {
        auto& __proxy = static_cast<__proxy_base&>(__receiver);
        if constexpr (__usable_stoppable_schedule<Sch>) {
            if (__proxy.__cancellable) {
                __start_child<true>(__proxy, __storage);
                return;
            }
        }
        // If the stored lvalue cannot schedule infallibly with a token,
        // connect its sender to a wrapper with env<> instead.
        __start_child<false>(__proxy, __storage);
    }

    // The stage-1 task domain leaves bulk on the default serial path.
    void schedule_bulk_chunked(std::size_t, __spi::bulk_item_receiver_proxy&,
                               std::span<std::byte>) noexcept override { std::terminate(); }
    void schedule_bulk_unchunked(std::size_t, __spi::bulk_item_receiver_proxy&,
                                 std::span<std::byte>) noexcept override { std::terminate(); }
};

template<class Sch, class Allocator>
std::shared_ptr<__backend> __make_backend(Sch&& __scheduler, const Allocator& __alloc) {
    using __sched = std::remove_cvref_t<Sch>;
    using __model = __backend_for<__sched, Allocator>;
    if constexpr (std::same_as<__sched, inline_scheduler> &&
                  std::same_as<Allocator, std::allocator<void>>) {
        // Neither inline_scheduler nor the default allocator exposes its
        // instance identity. An empty-owner alias needs no control block.
        // Immortal byte storage needs no exit-time destruction.
        alignas(__model) static std::byte __bytes[sizeof(__model)];
        static __model* const __instance =
            ::new (static_cast<void*>(__bytes)) __model(std::forward<Sch>(__scheduler), __alloc);
        return std::shared_ptr<__backend>(std::shared_ptr<__backend>{}, __instance);
    } else {
        return std::allocate_shared<__model>(__alloc, std::forward<Sch>(__scheduler), __alloc);
    }
}

template<class Token>
class __translated_stop {
    using __forwarder = __stop::__proxy_stop_forwarder<__stop::__no_proxy_stop_observer>;
    static_assert(__stop::__proxy_stoppable_callback<Token, __forwarder>,
                  "the token must support the receiver proxy's forwarding callback");

    std::shared_ptr<__stop::__proxy_stop_cell> __cell_;
    std::optional<std::stop_callback_for_t<Token, __forwarder>> __callback_;

public:
    explicit __translated_stop(const __backend& __owner) : __cell_(__owner.__make_stop_cell()) {}

    std::inplace_stop_token __prepare(Token __upstream) noexcept {
        if (!__upstream.stop_possible()) return {};
        __cell_->emplace();
        __callback_.emplace(std::move(__upstream), __forwarder{__cell_, {}});
        return (*__cell_)->get_token();
    }

    void __finish() noexcept { __callback_.reset(); }
};

struct __no_translation {
    explicit __no_translation(const __backend&) noexcept {}
};

template<class Receiver>
class __proxy final : public __proxy_base {
    using __token_type = std::stop_token_of_t<env_of_t<Receiver>>;
    static constexpr bool __can_stop = !std::unstoppable_token<__token_type>;
    static constexpr bool __translate = __can_stop && !std::same_as<__token_type, std::inplace_stop_token>;

    [[no_unique_address]] Receiver __receiver_;
    [[no_unique_address]] std::conditional_t<__translate, __translated_stop<__token_type>,
                                            __no_translation> __stop_;

public:
    explicit __proxy(Receiver&& __receiver, const __backend& __owner)
        : __proxy_base(__can_stop), __receiver_(std::move(__receiver)), __stop_(__owner) {}

    ~__proxy() { __destroy_child(); }

    void __prepare() noexcept {
        // Publish the answer before wrapped connect/start can see this proxy.
        // No lazy registration (and no allocation) in the noexcept query.
        if constexpr (__translate) {
            __token_ = __stop_.__prepare(__stop::__read_proxy_env<Receiver>{__receiver_}());
        } else if constexpr (__can_stop) {
            __token_ = __stop::__read_proxy_env<Receiver>{__receiver_}();
        }
    }

    void set_value() noexcept override {
        if constexpr (__translate) __stop_.__finish();
        std::execution::set_value(std::move(__receiver_));
    }

    void set_stopped() noexcept override {
        if constexpr (__can_stop) {
            if constexpr (__translate) __stop_.__finish();
            std::execution::set_stopped(std::move(__receiver_));
        } else {
            std::terminate();
        }
    }

    void set_error(std::exception_ptr) noexcept override { std::terminate(); }
};

template<class Receiver>
class __operation {
    std::shared_ptr<__backend> __backend_;
    alignas(std::max_align_t) std::byte __storage_[64];
    __proxy<Receiver> __proxy_;

public:
    using operation_state_concept = operation_state_tag;

    explicit __operation(std::shared_ptr<__backend> __owner, Receiver&& __receiver)
        : __backend_(std::move(__owner)), __proxy_(std::move(__receiver), *__backend_) {}

    __operation(const __operation&) = delete;
    __operation& operator=(const __operation&) = delete;

    void start() & noexcept {
        __proxy_.__prepare();
        auto __backend_pin = __backend_;
        __backend_pin->schedule(__proxy_, __storage_);
    }
};

template<class Env>
concept __stoppable_env = !std::unstoppable_token<std::stop_token_of_t<Env>>;

template<class Env>
using __signatures = std::conditional_t<__stoppable_env<Env>,
    completion_signatures<set_value_t(), set_stopped_t()>, completion_signatures<set_value_t()>>;

} // namespace __forge_task_scheduler

class task_scheduler;

namespace __forge_task_scheduler {

template<class Sch>
concept __binds_eraser_base =
    std::is_convertible_v<std::remove_reference_t<Sch>*, const task_scheduler*>;

template<class Sch>
concept __known_scheduler =
    std::same_as<Sch, __forge_affine::__unstoppable_scheduler<task_scheduler>>;

} // namespace __forge_task_scheduler

class task_scheduler {
    using __backend = __forge_task_scheduler::__backend;
    struct __ts_domain : default_domain {};
    struct __sender;

    std::shared_ptr<__backend> __backend_;

public:
    using scheduler_concept = scheduler_tag;

    // Checking a derived eraser's copyability can consider this inherited
    // template. Defer its scheduler check to the body without removing the
    // erasing overload. [over.match.funcs] excludes the inherited template
    // when copying the derived type itself.
    template<class Sch, class Allocator = std::allocator<void>>
        requires (!std::same_as<task_scheduler, std::remove_cvref_t<Sch>>) &&
                 (std::derived_from<std::remove_cvref_t<Sch>, task_scheduler> || scheduler<Sch>)
    explicit task_scheduler(Sch&& __scheduler, Allocator __alloc = {}) {
        if constexpr (!scheduler<Sch>) {
            static_assert(scheduler<Sch>, "task_scheduler requires a scheduler");
        } else {
            constexpr bool __infallible = __forge_affine::__infallible_scheduler<Sch, env<>>;
            static_assert(__infallible, "task_scheduler requires an infallible scheduler in env<>");
            if constexpr (__infallible) {
                __backend_ = __forge_task_scheduler::__make_backend(std::forward<Sch>(__scheduler), __alloc);
            }
        }
    }

    task_scheduler(const task_scheduler&) = default;
    task_scheduler& operator=(const task_scheduler&) = default;

    [[nodiscard]] __sender schedule() noexcept;

    forward_progress_guarantee query(get_forward_progress_guarantee_t) const noexcept {
        return __backend_->__progress();
    }

    task_scheduler query(get_completion_scheduler_t<set_value_t>) const noexcept { return *this; }
    __ts_domain query(get_completion_domain_t<set_value_t>) const noexcept { return {}; }

    friend bool operator==(const task_scheduler& __lhs, const task_scheduler& __rhs) noexcept {
        return __lhs.__backend_->__type() == __rhs.__backend_->__type() &&
               __lhs.__backend_->__equals(__rhs.__backend_->__scheduler_address());
    }

    // Keep the draft's fixed left parameter for overload ranking.
    // Checking scheduler<X> looks up X == X. If task_scheduler is an
    // associated class of X, that lookup finds this operator with Sch = X and
    // asks scheduler<X> again while it is being decided, which GCC rejects.
    // Let affine's exact wrapper participate without checking it again.
    // A derived eraser stays a concrete right operand only if it cannot
    // bind to the eraser-to-eraser overload, as when volatile-qualified.
    template<class Sch>
        requires __forge_task_scheduler::__known_scheduler<Sch> ||
                 ((!__forge_task_scheduler::__binds_eraser_base<Sch>) && scheduler<Sch>)
    friend bool operator==(const task_scheduler& __lhs, const Sch& __rhs) noexcept {
        if constexpr (std::is_volatile_v<Sch>) {
            return false; // The stored concrete type is always cv-unqualified.
        } else {
            return __lhs.__backend_->__type() == &__forge_task_scheduler::__type_key<Sch> &&
                   __lhs.__backend_->__equals(std::addressof(__rhs));
        }
    }
};

struct task_scheduler::__sender {
    using sender_concept = sender_tag;
    task_scheduler __scheduler;

    // Attributes are not themselves a scheduler. Otherwise the contextual
    // CPO fallbacks would supply answers even for absent channels.
    struct __attrs {
        task_scheduler __scheduler;

        task_scheduler query(get_completion_scheduler_t<set_value_t>) const noexcept { return __scheduler; }
        __ts_domain query(get_completion_domain_t<set_value_t>) const noexcept { return {}; }

        template<class Env, class... Rest>
            requires __forge_task_scheduler::__stoppable_env<Env>
        task_scheduler query(get_completion_scheduler_t<set_stopped_t>, Env&&, Rest&&...) const noexcept {
            return __scheduler;
        }

        template<class Env, class... Rest>
            requires __forge_task_scheduler::__stoppable_env<Env>
        __ts_domain query(get_completion_domain_t<set_stopped_t>, Env&&, Rest&&...) const noexcept { return {}; }
    };

    __attrs get_env() const noexcept { return {__scheduler}; }

    template<class Self, class Env>
    static constexpr auto get_completion_signatures() noexcept
        -> __forge_task_scheduler::__signatures<Env> { return {}; }

    template<receiver Receiver>
        requires __forge_receiver_of<Receiver,
            __forge_task_scheduler::__signatures<env_of_t<Receiver>>>
    auto connect(Receiver __receiver) const
        -> __forge_task_scheduler::__operation<Receiver> {
        return __forge_task_scheduler::__operation<Receiver>{
            __scheduler.__backend_, std::move(__receiver)};
    }
};

inline auto task_scheduler::schedule() noexcept -> __sender { return {*this}; }

} // namespace std::execution
