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

#include "concepts.hpp"

namespace std::execution {

namespace __forge_inline {

class inline_scheduler;

struct env {
    const inline_scheduler* sched_;
    friend auto tag_invoke(get_scheduler_t, const env& self) noexcept -> inline_scheduler;
};

template<class R>
struct operation : __forge_detail::__immovable {
    using operation_state_concept = operation_state_t;
    [[no_unique_address]] R rcvr_;

    explicit operation(R rcvr) : rcvr_(std::move(rcvr)) {}

    friend void tag_invoke(start_t, operation& self) noexcept { std::execution::set_value(std::move(self.rcvr_)); }
};

struct sender {
    using sender_concept = sender_t;
    const inline_scheduler* sched_ = nullptr;

    friend auto tag_invoke(get_completion_signatures_t, const sender&, auto) noexcept
        -> completion_signatures<set_value_t()> {
        return {};
    }

    template<receiver R>
    friend auto tag_invoke(connect_t, sender s, R rcvr) -> operation<R> {
        return operation<R>(std::move(rcvr));
    }

    friend auto tag_invoke(get_env_t, const sender& self) noexcept -> env { return env{self.sched_}; }
};

} // namespace __forge_inline

class __forge_inline::inline_scheduler {
public:
    using scheduler_concept = scheduler_t;

    inline_scheduler() noexcept = default;

    [[nodiscard]] __forge_inline::sender schedule() const noexcept { return __forge_inline::sender{this}; }

    friend auto tag_invoke(schedule_t, const inline_scheduler& self) noexcept -> __forge_inline::sender {
        return __forge_inline::sender{&self};
    }

    bool operator==(const inline_scheduler&) const noexcept = default;
};

namespace __forge_inline {
inline auto tag_invoke(get_scheduler_t, const env& self) noexcept -> inline_scheduler { return *self.sched_; }
} // namespace __forge_inline

using inline_scheduler = __forge_inline::inline_scheduler;


} // namespace std::execution
