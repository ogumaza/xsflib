// SPDX-License-Identifier: MIT

// The bytes of a render's samples, and their SHA-256, for tests that check a render against hashes recorded from
// another source. A test that includes this header links xsflib::common for Sha256 (xsflib/sha256.h).

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

// `samples` as 16-bit little-endian bytes, in their order. Stereo frames come left then right.
inline std::vector<uint8_t> SampleBytes(std::span<const int16_t> samples)
{
    std::vector<uint8_t> bytes(2 * samples.size());
    for (std::size_t i = 0; i < samples.size(); ++i)
    {
        StoreLe16(bytes.data() + 2 * i, static_cast<uint16_t>(samples[i]));
    }

    return bytes;
}

// The SHA-256 of `frames` stereo frames of `samples` from frame `first`, as 16-bit little-endian samples, left then
// right (SampleBytes): 64 lowercase hex digits. The frames have to lie within `samples`.
inline std::string SamplesSha256(std::span<const int16_t> samples, std::size_t first, std::size_t frames)
{
    return Sha256::Hex(Sha256::Of(SampleBytes(samples.subspan(2 * first, 2 * frames))));
}

} // namespace xsflib::testing
