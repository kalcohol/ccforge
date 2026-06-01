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
#include <cstdint>

namespace forge::accel {

struct device_id {
    std::uint32_t value = 0;

    friend auto operator==(device_id, device_id) -> bool = default;
};

struct device_info {
    device_id id{};
    std::uint32_t ordinal = 0;
    bool available = true;
};

enum class memory_kind {
    host,
    pinned_host,
    mapped_host,
    device,
    cached_device,
    managed
};

enum class queue_kind {
    general,
    compute,
    copy,
    command
};

enum class copy_kind {
    host_to_device,
    device_to_host,
    device_to_device,
    host_to_host
};

struct model_io_info {
    std::size_t inputs = 0;
    std::size_t outputs = 0;
};

} // namespace forge::accel
