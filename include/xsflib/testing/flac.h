// SPDX-License-Identifier: MIT

// Reads the FLAC recordings that tests compare renders with. A target that includes this header links xsflib::flac
// (libFLAC). No other xsflib target requires libFLAC for it.

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <FLAC/stream_decoder.h>

#include "xsflib/files.h"

namespace xsflib::testing
{

// Decodes a FLAC stream of 16-bit stereo at `rate` samples a second into interleaved samples. Throws
// std::runtime_error if `bytes` aren't such a stream, or if the samples don't match the MD5 signature of the stream.
// `name` names the stream in the error.
inline std::vector<int16_t> DecodeFlac(std::span<const uint8_t> bytes, uint32_t rate, const std::string& name)
{
    struct Decoding
    {
        std::span<const uint8_t> bytes;
        std::size_t at = 0;
        uint32_t rate = 0;
        std::vector<int16_t> samples;
        bool bad = false;
    };

    Decoding decoding{bytes, 0, rate, {}, false};
    const auto read = [](const FLAC__StreamDecoder*, FLAC__byte buffer[], std::size_t* size,
                         void* client) -> FLAC__StreamDecoderReadStatus
    {
        auto& d = *static_cast<Decoding*>(client);
        const std::size_t count = std::min(*size, d.bytes.size() - d.at);
        if (count != 0)
        {
            std::memcpy(buffer, d.bytes.data() + d.at, count);
        }
        d.at += count;
        *size = count;

        return count == 0 ? FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM : FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
    };
    const auto write = [](const FLAC__StreamDecoder*, const FLAC__Frame* frame, const FLAC__int32* const buffer[],
                          void* client) -> FLAC__StreamDecoderWriteStatus
    {
        auto& d = *static_cast<Decoding*>(client);
        if (frame->header.channels != 2 || frame->header.bits_per_sample != 16 || frame->header.sample_rate != d.rate)
        {
            d.bad = true;
            return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
        }

        for (uint32_t i = 0; i < frame->header.blocksize; ++i)
        {
            d.samples.push_back(static_cast<int16_t>(buffer[0][i]));
            d.samples.push_back(static_cast<int16_t>(buffer[1][i]));
        }

        return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
    };
    const auto error = [](const FLAC__StreamDecoder*, FLAC__StreamDecoderErrorStatus, void* client)
    {
        static_cast<Decoding*>(client)->bad = true;
    };

    FLAC__StreamDecoder* decoder = FLAC__stream_decoder_new();
    if (decoder != nullptr)
    {
        FLAC__stream_decoder_set_md5_checking(decoder, true);
    }
    bool decoded = decoder != nullptr &&
                   FLAC__stream_decoder_init_stream(decoder, read, nullptr, nullptr, nullptr, nullptr, write, nullptr,
                                                    error, &decoding) == FLAC__STREAM_DECODER_INIT_STATUS_OK &&
                   FLAC__stream_decoder_process_until_end_of_stream(decoder);
    if (decoder != nullptr)
    {
        decoded &= FLAC__stream_decoder_finish(decoder) != 0;
        FLAC__stream_decoder_delete(decoder);
    }

    if (!decoded || decoding.bad)
    {
        throw std::runtime_error("can't decode " + name);
    }

    return decoding.samples;
}

// Reads a FLAC file of 16-bit stereo at `rate` samples a second into interleaved samples. Throws std::runtime_error if
// the file can't be read, or as DecodeFlac does.
inline std::vector<int16_t> ReadFlac(const std::filesystem::path& path, uint32_t rate)
{
    std::vector<uint8_t> bytes;
    if (!ReadFile(path, bytes))
    {
        throw std::runtime_error("can't read " + Utf8(path));
    }

    return DecodeFlac(bytes, rate, Utf8(path));
}

} // namespace xsflib::testing
