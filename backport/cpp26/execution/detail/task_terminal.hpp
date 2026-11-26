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

#include <concepts>
#include <coroutine>
#include <cstddef>
#include <exception>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

#include "../concepts.hpp"

// Frame ownership and terminal delivery of std::execution::task.
// Protocol model: formal/task_frame. Every edit needs a lock refresh.
//
// One owner holds the suspended coroutine frame at any time: the task, then
// the connected operation. A terminal exchanges the handle out, destroys the
// frame and only then enters the receiver ([task.promise]/3, /5 and /8). The
// receiver may destroy the operation inside that call, so nothing after a
// delivery may touch the frame, the awaiter, the receiver or the operation.

namespace std::execution::__forge_task {

template<class Promise>
class __frame_owner {
    std::coroutine_handle<Promise> __handle_;

public:
    explicit __frame_owner(std::coroutine_handle<Promise> __handle) noexcept
        : __handle_(__handle) {}

    // [task.members]/1
    __frame_owner(__frame_owner&& __other) noexcept
        : __handle_(__other.__release()) {}

    __frame_owner& operator=(__frame_owner&&) = delete;

    // [task.members]/2 and [task.state]/3
    ~__frame_owner() {
        __destroy();
    }

    explicit operator bool() const noexcept {
        return static_cast<bool>(__handle_);
    }

    std::coroutine_handle<Promise> __get() const noexcept {
        return __handle_;
    }

    std::coroutine_handle<Promise> __release() noexcept {
        return std::exchange(__handle_, {});
    }

    // The owner is already empty while the frame's destructors run.
    void __destroy() noexcept {
        if (auto __frame = __release()) {
            __frame.destroy();
        }
    }
};

// The result lives in the operation, outside the frame. The slot does not use
// std::optional: a task may return nullopt_t, in_place_t or an rvalue
// reference, which optional excludes.
template<class T>
class __result_slot {
    union {
        T __value_;
    };
    bool __engaged_ = false;

public:
    __result_slot() noexcept {}
    __result_slot(const __result_slot&) = delete;
    __result_slot& operator=(const __result_slot&) = delete;

    ~__result_slot() {
        if (__engaged_) {
            std::destroy_at(std::addressof(__value_));
        }
    }

    // As optional::emplace: a second co_return, reached after a caught
    // scope-exit exception, replaces the stored result. If the replacement
    // throws, the slot stays empty.
    template<class V>
    void __emplace(V&& __operand) {
        if (__engaged_) {
            __engaged_ = false;
            std::destroy_at(std::addressof(__value_));
        }
        ::new (static_cast<void*>(std::addressof(__value_))) T(static_cast<V&&>(__operand));
        __engaged_ = true;
    }

    T&& __take() noexcept {
        return static_cast<T&&>(__value_);
    }
};

// A reference result keeps the referent's address, which preserves identity,
// cv-qualification and category. The caller owns the referent's lifetime.
template<class U>
class __result_slot<U&> {
    U* __address_ = nullptr;

public:
    template<class V>
    void __emplace(V&& __operand) {
        U& __referent(static_cast<V&&>(__operand));
        __address_ = std::addressof(__referent);
    }

    U& __take() noexcept {
        return *__address_;
    }
};

template<class U>
class __result_slot<U&&> {
    U* __address_ = nullptr;

public:
    template<class V>
    void __emplace(V&& __operand) {
        U&& __referent(static_cast<V&&>(__operand));
        __address_ = std::addressof(__referent);
    }

    U&& __take() noexcept {
        return static_cast<U&&>(*__address_);
    }
};

template<>
class __result_slot<void> {};

template<class T>
struct __outcome {
    [[no_unique_address]] __result_slot<T> __result;
    std::exception_ptr __exception;
};

template<class Wanted, class... Errors>
constexpr std::size_t __error_position() noexcept {
    std::size_t __position = 0;
    (void)((std::same_as<Wanted, Errors> ? true : (++__position, false)) || ...);
    return __position;
}

template<class... Errors>
inline constexpr bool __lists_exception_ptr = (std::same_as<std::exception_ptr, Errors> || ...);

// What a promise sees of its connected operation. The operation implements
// the three deliveries with its receiver, which this interface does not name.
template<class Promise, class T, class... Errors>
class __terminal_core {
public:
    __frame_owner<Promise> __frame_;
    __outcome<T>* __outcome_ = nullptr;

    explicit __terminal_core(std::coroutine_handle<Promise> __handle) noexcept
        : __frame_(__handle) {}

    __terminal_core(const __terminal_core&) = delete;
    __terminal_core& operator=(const __terminal_core&) = delete;

    // [task.state]/5. A completion inside the resumption may destroy *this.
    void __resume() noexcept {
        __frame_.__get().resume();
    }

    // [task.promise]/3
    void __complete_result() noexcept {
        __retire_frame();
        __deliver_result();
    }

    // [task.promise]/8
    void __complete_stopped() noexcept {
        __retire_frame();
        __deliver_stopped();
    }

    // [task.promise]/5. The caller's staged payload outlives the frame and
    // the operation. The conversion runs after the frame is destroyed, so a
    // conversion failure can no longer reach the promise: it goes to the
    // exception_ptr channel if the task lists one, and terminates otherwise.
    template<class Cerr, class Staged>
    void __complete_error(Staged& __staged) noexcept {
        __retire_frame();
        bool __entered = false;
        std::exception_ptr __failure;
        try {
            __forward_error<Cerr>(Cerr(std::move(__staged)), __entered);
            return;
        } catch (...) {
            // Once the receiver was entered, only the destructor of the
            // converted temporary can throw. There is no second terminal.
            if (__entered || !__lists_exception_ptr<Errors...>) {
                std::terminate();
            }
            __failure = std::current_exception();
        }
        // Outside the handler, with the conversion failure as the error.
        if constexpr (__lists_exception_ptr<Errors...>) {
            __forward_error<std::exception_ptr>(std::move(__failure), __entered);
        }
    }

protected:
    ~__terminal_core() = default;

private:
    void __retire_frame() noexcept {
        auto __frame = __frame_.__release();
        __frame.destroy();
    }

    // The erased argument is the address of a typed pointer object. An error
    // type may be a function reference, whose address is not an object pointer.
    template<class Cerr>
    void __forward_error(Cerr&& __converted, bool& __entered) noexcept {
        std::add_pointer_t<Cerr> __referent = std::addressof(__converted);
        __entered = true;
        __deliver_error(__error_position<Cerr, Errors...>(), std::addressof(__referent));
    }

    virtual void __deliver_result() noexcept = 0;
    virtual void __deliver_stopped() noexcept = 0;
    virtual void __deliver_error(std::size_t __index, void* __referent) noexcept = 0;
};

// Receiver entry. The stored exception leaves the operation first, so the
// argument stays valid if the receiver destroys the operation.
template<class Rcvr, class T, class... Errors>
void __send_result(Rcvr& __rcvr, __outcome<T>& __data) noexcept {
    if constexpr (__lists_exception_ptr<Errors...>) {
        if (__data.__exception) {
            auto __stored = std::move(__data.__exception);
            std::execution::set_error(std::move(__rcvr), std::move(__stored));
            return;
        }
    }
    if constexpr (std::is_void_v<T>) {
        std::execution::set_value(std::move(__rcvr));
    } else {
        std::execution::set_value(std::move(__rcvr), __data.__result.__take());
    }
}

template<class Rcvr>
void __send_stopped(Rcvr& __rcvr) noexcept {
    std::execution::set_stopped(std::move(__rcvr));
}

// __referent points to an object of type add_pointer_t<E> for the listed
// error type E at __index.
template<class Rcvr, class... Errors>
void __send_error(Rcvr& __rcvr, std::size_t __index, void* __referent) noexcept {
    std::size_t __position = 0;
    (void)((__position++ == __index
        ? (std::execution::set_error(std::move(__rcvr),
               static_cast<Errors&&>(**static_cast<std::add_pointer_t<Errors>*>(__referent))), true)
        : false) || ...);
}

// [task.promise]/3. The awaiter lives in the frame it is about to destroy.
template<class Core>
struct __final_awaiter {
    Core* __core_;

    constexpr bool await_ready() const noexcept {
        return false;
    }

    void await_suspend(std::coroutine_handle<>) const noexcept {
        Core* const __core = __core_;
        __core->__complete_result();
    }

    constexpr void await_resume() const noexcept {}
};

// [task.promise]/5 with the two staging moves. The first move fills
// __payload_ in the frame. The second moves it to the await_suspend stack. A
// throwing move leaves the frame alive and resumes the body with the exception.
template<class Core, class Payload, class Cerr>
struct __error_awaiter {
    Core* __core_;
    Payload __payload_;

    __error_awaiter(Core* __core, Payload&& __source)
        : __core_(__core), __payload_(std::move(__source)) {}

    constexpr bool await_ready() const noexcept {
        return false;
    }

    void await_suspend(std::coroutine_handle<>) {
        Core* const __core = __core_;
        Payload __staged(std::move(__payload_));
        static_assert(noexcept(__core->template __complete_error<Cerr>(__staged)));
        __core->template __complete_error<Cerr>(__staged);
    }

    constexpr void await_resume() const noexcept {}
};

} // namespace std::execution::__forge_task
