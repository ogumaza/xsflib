// SPDX-License-Identifier: MIT

// The timing helpers of the length analysis (see length.h).

#include "xsflib/rip/length.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

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

std::optional<Timing> TimingOfPiece(const std::vector<std::optional<Timing>>& timings)
{
    const auto missing = [](const std::optional<Timing>& timing)
    {
        return !timing;
    };
    if (timings.empty() || std::ranges::any_of(timings, missing))
    {
        return std::nullopt;
    }

    std::optional<Timing> piece;
    for (const std::optional<Timing>& timing : timings)
    {
        if (timing->silent)
        {
            continue;
        }

        if (!piece)
        {
            piece = timing;
            continue;
        }

        piece->length_ms = std::max(piece->length_ms, timing->length_ms);
        piece->fade_ms = std::max(piece->fade_ms, timing->fade_ms);
    }

    return piece ? piece : timings.front();
}

} // namespace xsflib::rip
