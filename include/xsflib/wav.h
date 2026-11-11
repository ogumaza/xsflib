// SPDX-License-Identifier: MIT

// WAV files of 16-bit PCM, and the header of a WAV file of PCM or IEEE float samples.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include "xsflib/bytes.h"

namespace xsflib
{

// The format tags of a WAV file's format chunk: PCM samples and IEEE float samples.
inline constexpr uint16_t kWavPcm = 1;
inline constexpr uint16_t kWavFloat = 3;

// The 44 bytes that start a WAV file: the RIFF header, the format chunk and the data chunk's header. The data chunk
// holds `data_bytes` bytes of samples of `bits` bits each, `channels` channels interleaved, at `rate` frames a second.
// `format_tag` is kWavPcm or kWavFloat, and `bits` is a multiple of 8. A WAV file's sizes are 32-bit. `data_bytes` can
// be up to 2^32 - 37.
inline std::array<uint8_t, 44> WavHeader(uint16_t format_tag, uint16_t channels, uint32_t rate, uint16_t bits,
                                         uint32_t data_bytes)
{
    const uint32_t frame_bytes = uint32_t{channels} * (bits / 8u);
    std::array<uint8_t, 44> header{};
    uint8_t* p = header.data();

    std::memcpy(p, "RIFF", 4);
    StoreLe32(p + 4, 36 + data_bytes);
    std::memcpy(p + 8, "WAVEfmt ", 8);
    StoreLe32(p + 16, 16);
    StoreLe16(p + 20, format_tag);
    StoreLe16(p + 22, channels);
    StoreLe32(p + 24, rate);
    StoreLe32(p + 28, rate * frame_bytes);
    StoreLe16(p + 32, static_cast<uint16_t>(frame_bytes));
    StoreLe16(p + 34, bits);
    std::memcpy(p + 36, "data", 4);
    StoreLe32(p + 40, data_bytes);

    return header;
}

// A WAV file of `samples`: 16-bit PCM, `channels` channels interleaved, at `rate` frames a second. A WAV file's sizes
// are 32-bit. `samples` can take up to 2^32 - 37 bytes.
inline std::vector<uint8_t> WavFile(std::span<const int16_t> samples, uint16_t channels, uint32_t rate)
{
    const auto data_bytes = static_cast<uint32_t>(samples.size() * 2);
    std::vector<uint8_t> file(44 + std::size_t{data_bytes});
    const std::array<uint8_t, 44> header = WavHeader(kWavPcm, channels, rate, 16, data_bytes);
    std::memcpy(file.data(), header.data(), header.size());

    uint8_t* samples_start = file.data() + header.size();
    for (std::size_t i = 0; i < samples.size(); i++)
    {
        StoreLe16(samples_start + 2 * i, static_cast<uint16_t>(samples[i]));
    }

    return file;
}

} // namespace xsflib
