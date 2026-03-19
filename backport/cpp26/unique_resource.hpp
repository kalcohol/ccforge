// MIT License
//
// Copyright (c) 2026 Forge Project
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

#include <functional>
#include <type_traits>
#include <utility>

namespace std {

template<class R, class D>
class unique_resource;

template<class R, class D, class S = decay_t<R>>
unique_resource<decay_t<R>, decay_t<D>>
make_unique_resource_checked(R&& r, const S& invalid, D&& d)
    noexcept(is_nothrow_constructible_v<decay_t<R>, R> &&
             is_nothrow_constructible_v<decay_t<D>, D>);

template<class R, class D>
class unique_resource {
    using resource_storage = conditional_t<
        is_reference_v<R>,
        reference_wrapper<remove_reference_t<R>>,
        R>;

    struct construct_tag {};

public:
    template<class RR, class DD>
        requires (is_constructible_v<resource_storage, RR> &&
                  is_constructible_v<D, DD> &&
                  (is_nothrow_constructible_v<resource_storage, RR> ||
                   is_constructible_v<resource_storage, RR&>) &&
                  (is_nothrow_constructible_v<D, DD> ||
                   is_constructible_v<D, DD&>))
    unique_resource(RR&& r, DD&& d) noexcept(
        (is_nothrow_constructible_v<resource_storage, RR> ||
         is_nothrow_constructible_v<resource_storage, RR&>) &&
        (is_nothrow_constructible_v<D, DD> ||
         is_nothrow_constructible_v<D, DD&>))
        : unique_resource(construct_tag{}, std::forward<RR>(r), std::forward<DD>(d), true) {}

    unique_resource(const unique_resource&) = delete;
    unique_resource& operator=(const unique_resource&) = delete;

    unique_resource(unique_resource&& other) noexcept(
        is_nothrow_move_constructible_v<resource_storage> &&
        is_nothrow_move_constructible_v<D>)
        : resource_(construct_resource_from_other(other))
        , deleter_(construct_deleter_from_other(other, resource_))
        , execute_on_reset_(std::exchange(other.execute_on_reset_, false)) {}

    unique_resource& operator=(unique_resource&& other) noexcept(
        is_nothrow_move_assignable_v<R> &&
        is_nothrow_move_assignable_v<D>) {
        if (this != &other) {
            reset();

            if constexpr (is_nothrow_move_assignable_v<R> ||
                          !is_copy_assignable_v<R>) {
                if constexpr (is_nothrow_move_assignable_v<D> ||
                              !is_copy_assignable_v<D>) {
                    resource_ = std::move(other.resource_);
                    deleter_ = std::move(other.deleter_);
                } else {
                    deleter_ = other.deleter_;
                    resource_ = std::move(other.resource_);
                }
            } else {
                if constexpr (is_nothrow_move_assignable_v<D> ||
                              !is_copy_assignable_v<D>) {
                    resource_ = other.resource_;
                    deleter_ = std::move(other.deleter_);
                } else {
                    resource_ = other.resource_;
                    deleter_ = other.deleter_;
                }
            }

            execute_on_reset_ = std::exchange(other.execute_on_reset_, false);
        }

        return *this;
    }

    ~unique_resource() noexcept {
        reset();
    }

    void reset() noexcept {
        if (execute_on_reset_) {
            execute_on_reset_ = false;
            std::invoke(deleter_, resource_value(resource_));
        }
    }

    template<class RR>
    void reset(RR&& r) {
        static_assert(
            is_assignable_v<resource_storage&, RR> ||
            is_assignable_v<resource_storage&, const remove_reference_t<RR>&>,
            "reset(RR&&) requires resource to be assignable from RR");
        reset();

        try {
            if constexpr (is_nothrow_assignable_v<resource_storage&, RR>) {
                resource_ = std::forward<RR>(r);
            } else {
                resource_ = std::as_const(r);
            }

            execute_on_reset_ = true;
        } catch (...) {
            std::invoke(deleter_, r);
            throw;
        }
    }

    void release() noexcept {
        execute_on_reset_ = false;
    }

    const R& get() const noexcept {
        return resource_value(resource_);
    }

    const D& get_deleter() const noexcept {
        return deleter_;
    }

    template<class T = R>
        requires (is_pointer_v<T> && !is_void_v<remove_pointer_t<T>>)
    add_lvalue_reference_t<remove_pointer_t<T>> operator*() const noexcept(noexcept(*declval<const T&>())) {
        return *get();
    }

    template<class T = R>
        requires (is_pointer_v<T>)
    T operator->() const noexcept {
        return get();
    }

    void swap(unique_resource& other)
        noexcept(is_nothrow_swappable_v<resource_storage> && is_nothrow_swappable_v<D>)
        requires (is_swappable_v<resource_storage> && is_swappable_v<D>) {
        using std::swap;
        swap(resource_, other.resource_);
        swap(deleter_, other.deleter_);
        swap(execute_on_reset_, other.execute_on_reset_);
    }

private:
    static R& resource_value(resource_storage& resource) noexcept {
        if constexpr (is_reference_v<R>) {
            return resource.get();
        } else {
            return resource;
        }
    }

    static const R& resource_value(const resource_storage& resource) noexcept {
        if constexpr (is_reference_v<R>) {
            return resource.get();
        } else {
            return resource;
        }
    }

    template<class RR>
    static resource_storage construct_resource(RR&& r) {
        if constexpr (is_nothrow_constructible_v<resource_storage, RR> ||
                      !is_constructible_v<resource_storage, RR&>) {
            return resource_storage(std::forward<RR>(r));
        } else {
            return resource_storage(r);
        }
    }

    template<class DD>
    static D construct_deleter(DD&& d) {
        if constexpr (is_nothrow_constructible_v<D, DD> ||
                      !is_constructible_v<D, DD&>) {
            return D(std::forward<DD>(d));
        } else {
            return D(d);
        }
    }

    template<class RR, class DD>
    static resource_storage construct_resource_from_input(RR&& r, DD& d, bool execute_on_reset) {
        if constexpr (is_nothrow_constructible_v<resource_storage, RR> ||
                      !is_constructible_v<resource_storage, RR&>) {
            // By the constructor constraints, this path is noexcept.
            return resource_storage(std::forward<RR>(r));
        } else {
            try {
                // Move construction may throw, so we copy from an lvalue.
                return resource_storage(r);
            } catch (...) {
                if (execute_on_reset) {
                    std::invoke(d, r);
                }
                throw;
            }
        }
    }

    template<class DD>
    static D construct_deleter_from_input(DD&& d, const resource_storage& resource, bool execute_on_reset) {
        if constexpr (is_nothrow_constructible_v<D, DD> ||
                      !is_constructible_v<D, DD&>) {
            // By the constructor constraints, this path is noexcept.
            return D(std::forward<DD>(d));
        } else {
            try {
                // Move construction may throw, so we copy from an lvalue.
                return D(d);
            } catch (...) {
                if (execute_on_reset) {
                    std::invoke(d, resource_value(resource));
                }
                throw;
            }
        }
    }

    static resource_storage construct_resource_from_other(unique_resource& other) {
        if constexpr (is_nothrow_move_constructible_v<resource_storage> ||
                      !is_copy_constructible_v<resource_storage>) {
            return resource_storage(std::move(other.resource_));
        } else {
            return resource_storage(other.resource_);
        }
    }

    static D construct_deleter_from_other(unique_resource& other, const resource_storage& resource) {
        try {
            if constexpr (is_nothrow_move_constructible_v<D> || !is_copy_constructible_v<D>) {
                return D(std::move(other.deleter_));
            } else {
                return D(other.deleter_);
            }
        } catch (...) {
            if (other.execute_on_reset_) {
                std::invoke(other.deleter_, resource_value(resource));
                other.release();
            }
            throw;
        }
    }

    template<class RR, class DD>
    unique_resource(construct_tag, RR&& r, DD&& d, bool execute_on_reset) noexcept(
        (is_nothrow_constructible_v<resource_storage, RR> ||
         is_nothrow_constructible_v<resource_storage, RR&>) &&
        (is_nothrow_constructible_v<D, DD> ||
         is_nothrow_constructible_v<D, DD&>))
        : resource_(construct_resource_from_input(std::forward<RR>(r), d, execute_on_reset))
        , deleter_(construct_deleter_from_input(std::forward<DD>(d), resource_, execute_on_reset))
        , execute_on_reset_(execute_on_reset) {}

    template<class RR, class DD, class S>
    friend unique_resource<decay_t<RR>, decay_t<DD>>
    make_unique_resource_checked(RR&& r, const S& invalid, DD&& d)
        noexcept(is_nothrow_constructible_v<decay_t<RR>, RR> &&
                 is_nothrow_constructible_v<decay_t<DD>, DD>);

    resource_storage resource_;
    D deleter_;
    bool execute_on_reset_;
};

template<class R, class D>
void swap(unique_resource<R, D>& lhs, unique_resource<R, D>& rhs)
    noexcept(is_nothrow_swappable_v<conditional_t<is_reference_v<R>, reference_wrapper<remove_reference_t<R>>, R>> &&
             is_nothrow_swappable_v<D>)
    requires (is_swappable_v<conditional_t<is_reference_v<R>, reference_wrapper<remove_reference_t<R>>, R>> &&
              is_swappable_v<D>) {
    lhs.swap(rhs);
}

template<class R, class D>
unique_resource(R, D) -> unique_resource<R, D>;

template<class R, class D, class S>
unique_resource<decay_t<R>, decay_t<D>>
make_unique_resource_checked(R&& r, const S& invalid, D&& d)
    noexcept(is_nothrow_constructible_v<decay_t<R>, R> &&
             is_nothrow_constructible_v<decay_t<D>, D>) {
    using result_type = unique_resource<decay_t<R>, decay_t<D>>;
    const bool execute_on_reset = !static_cast<bool>(r == invalid);
    return result_type(typename result_type::construct_tag{}, std::forward<R>(r), std::forward<D>(d), execute_on_reset);
}

} // namespace std
