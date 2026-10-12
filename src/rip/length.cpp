// SPDX-License-Identifier: MIT

// The timing helpers of the length analysis (see length.h).

#include "xsflib/rip/length.h"

#include <cstdint>

namespace xsflib::rip
{

Timing EndAt(uint64_t frame, double frame_ms, double tail_ms)
{
    return {static_cast<long long>(static_cast<double>(frame) * frame_ms + tail_ms + 0.5), 0};
}

Timing LoopAt(uint64_t frame, double frame_ms, double fade_ms)
{
    return {static_cast<long long>(static_cast<double>(frame) * frame_ms + 0.5), static_cast<long long>(fade_ms)};
}

} // namespace xsflib::rip
