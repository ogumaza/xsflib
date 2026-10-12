// SPDX-License-Identifier: MIT

// WAV and FLAC writers (see audio_out.h).

#include "xsflib/cli/audio_out.h"

#include <FLAC/stream_encoder.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "xsflib/ascii.h"
#include "xsflib/files.h"
#include "xsflib/wav.h"

namespace xsflib::cli
{
namespace
{

// A WAV file's sizes are 32-bit. The RIFF size counts 36 bytes of headers besides the audio.
constexpr uint64_t kMaxWavDataBytes = 0xffffffffu - 36;
constexpr const char* kWavTooLong = "a WAV file can't hold 4 GiB of audio; write FLAC instead";

// True if the file name's extension is .flac in any case.
bool IsFlac(const std::string& path)
{
    return AsciiLower(std::filesystem::path(path).extension().string()) == ".flac";
}

std::optional<std::string> WriteWav(const std::string& path, const std::vector<int16_t>& pcm, uint32_t rate)
{
    if (uint64_t{pcm.size()} * 2 > kMaxWavDataBytes)
    {
        return kWavTooLong;
    }

    return WriteFile(PathFromUtf8(path), WavFile(pcm, 2, rate));
}

std::optional<std::string> WriteFlac(const std::string& path, const std::vector<int16_t>& pcm, uint32_t rate)
{
    FLAC__StreamEncoder* enc = FLAC__stream_encoder_new();
    if (!enc)
    {
        return "can't start the FLAC encoder";
    }

    FLAC__stream_encoder_set_channels(enc, 2);
    FLAC__stream_encoder_set_bits_per_sample(enc, 16);
    FLAC__stream_encoder_set_sample_rate(enc, rate);
    FLAC__stream_encoder_set_compression_level(enc, 8);
    FLAC__stream_encoder_set_total_samples_estimate(enc, pcm.size() / 2);
    if (FLAC__stream_encoder_init_file(enc, path.c_str(), nullptr, nullptr) != FLAC__STREAM_ENCODER_INIT_STATUS_OK)
    {
        FLAC__stream_encoder_delete(enc);
        return "can't write " + path;
    }

    std::vector<FLAC__int32> buf(8192 * 2);
    bool ok = true;
    for (std::size_t pos = 0; pos < pcm.size() && ok; pos += buf.size())
    {
        const std::size_t n = std::min(buf.size(), pcm.size() - pos);
        for (std::size_t i = 0; i < n; i++)
        {
            buf[i] = pcm[pos + i];
        }
        ok = FLAC__stream_encoder_process_interleaved(enc, buf.data(), static_cast<unsigned>(n / 2));
    }

    ok = FLAC__stream_encoder_finish(enc) && ok;
    FLAC__stream_encoder_delete(enc);
    if (!ok)
    {
        return "can't write " + path;
    }

    return std::nullopt;
}

} // namespace

std::optional<std::string> WriteAudio(const std::string& path, const std::vector<int16_t>& pcm, uint32_t rate)
{
    return IsFlac(path) ? WriteFlac(path, pcm, rate) : WriteWav(path, pcm, rate);
}

std::optional<std::string> CheckAudioLength(const std::string& path, uint64_t frames)
{
    if (!IsFlac(path) && frames > kMaxWavDataBytes / 4)
    {
        return kWavTooLong;
    }

    return std::nullopt;
}

} // namespace xsflib::cli
