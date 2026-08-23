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

#include <forge/any_scheduler.hpp>
#include <forge/resource_policy.hpp>

#include <concepts>
#include <execution>
#include <memory_resource>
#include <stop_token>
#include <type_traits>
#include <utility>

namespace forge::io {

class executor_ref {
public:
    executor_ref() = default;

    template<class Scheduler>
        requires (!std::is_same_v<std::remove_cvref_t<Scheduler>, executor_ref>)
              && (!std::is_same_v<std::remove_cvref_t<Scheduler>, forge::any_scheduler>)
              && std::execution::scheduler<std::remove_cvref_t<Scheduler>>
    executor_ref(Scheduler&& scheduler)
        : scheduler_(static_cast<Scheduler&&>(scheduler))
    {}

    executor_ref(forge::any_scheduler scheduler) noexcept
        : scheduler_(std::move(scheduler))
    {}

    [[nodiscard]] explicit operator bool() const noexcept {
        return static_cast<bool>(scheduler_);
    }

    [[nodiscard]] auto schedule() const noexcept {
        return scheduler_.schedule();
    }

    [[nodiscard]] auto scheduler() const noexcept -> const forge::any_scheduler& {
        return scheduler_;
    }

private:
    forge::any_scheduler scheduler_{};
};

struct io_env {
    executor_ref executor{};
    std::inplace_stop_token stop_token{};
    std::pmr::memory_resource* memory = forge::default_memory_resource();
};

} // namespace forge::io
