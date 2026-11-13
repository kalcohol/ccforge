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

#include <type_traits>
#include <utility>

namespace std::execution::__forge_detail {

// An ordinary member is never a potentially-overlapping subobject, even when
// the holder is a tuple base or a variant alternative. Construct the operation
// from the factory's prvalue here so empty immovable operations need no move.
template<class Op>
struct __op_holder {
    Op __op;

    template<class Factory>
        requires std::is_same_v<std::invoke_result_t<Factory>, Op>
    explicit __op_holder(Factory&& factory)
        noexcept(std::is_nothrow_invocable_v<Factory>)
        : __op(std::forward<Factory>(factory)())
    {}
};

} // namespace std::execution::__forge_detail
