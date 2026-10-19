// SPDX-License-Identifier: MIT

// The SHA-256 of a render's samples, for tests that check a render against hashes recorded from another source. A test
// that includes this header links xsflib::common for Sha256 (xsflib/sha256.h).

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "xsflib/bytes.h"
#include "xsflib/sha256.h"

namespace xsflib::testing
{

// The SHA-256 of `frames` stereo frames of `samples` from frame `first`, as 16-bit little-endian samples, left then
// right: 64 lowercase hex digits. The frames have to lie within `samples`.
inline std::string SamplesSha256(std::span<const int16_t> samples, std::size_t first, std::size_t frames)
{
    const std::span<const int16_t> selected = samples.subspan(2 * first, 2 * frames);
    std::vector<uint8_t> bytes(2 * selected.size());
    for (std::size_t i = 0; i < selected.size(); ++i)
    {
        StoreLe16(bytes.data() + 2 * i, static_cast<uint16_t>(selected[i]));
    }

    return Sha256::Hex(Sha256::Of(bytes));
}

} // namespace xsflib::testing
