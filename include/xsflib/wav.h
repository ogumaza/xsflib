// SPDX-License-Identifier: MIT

// WAV files of 16-bit PCM.

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include "xsflib/bytes.h"

namespace xsflib
{

// A WAV file of `samples`: 16-bit PCM, `channels` channels interleaved, at `rate` frames a second. A WAV file's sizes
// are 32-bit. `samples` can take up to 2^32 - 37 bytes.
inline std::vector<uint8_t> WavFile(std::span<const int16_t> samples, uint16_t channels, uint32_t rate)
{
    const auto data_bytes = static_cast<uint32_t>(samples.size() * 2);
    std::vector<uint8_t> file(44 + std::size_t{data_bytes});
    uint8_t* p = file.data();

    // The RIFF header, the format chunk and the data chunk's header.
    std::memcpy(p, "RIFF", 4);
    StoreLe32(p + 4, 36 + data_bytes);
    std::memcpy(p + 8, "WAVEfmt ", 8);
    StoreLe32(p + 16, 16);
    StoreLe16(p + 20, 1);
    StoreLe16(p + 22, channels);
    StoreLe32(p + 24, rate);
    StoreLe32(p + 28, rate * 2 * channels);
    StoreLe16(p + 32, static_cast<uint16_t>(2 * channels));
    StoreLe16(p + 34, 16);
    std::memcpy(p + 36, "data", 4);
    StoreLe32(p + 40, data_bytes);

    for (std::size_t i = 0; i < samples.size(); i++)
    {
        StoreLe16(p + 44 + 2 * i, static_cast<uint16_t>(samples[i]));
    }

    return file;
}

} // namespace xsflib
