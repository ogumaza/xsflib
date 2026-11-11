// SPDX-License-Identifier: MIT

#include "xsflib/psf/descriptor.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "output_mode.h"
#include "xsflib/bytes.h"
#include "xsflib/psf/psf.h"
#include "xsflib/text.h"

namespace xsflib::psf
{

std::optional<std::string> CheckDescriptorVersion(std::string_view name, uint32_t version, uint32_t supported)
{
    if (version != supported)
    {
        return std::string(name) + " descriptor version " + std::to_string(version) +
               "; this player supports version " + std::to_string(supported);
    }

    return std::nullopt;
}

std::optional<std::string> CheckDescriptorFlags(std::string_view name, uint32_t flags, uint32_t required)
{
    if (flags != required)
    {
        const std::string expected =
            required == 0 ? "; version 1 has none" : "; version 1 requires only bit 0 (DSP required) to be set";
        return "the " + std::string(name) + " descriptor's flags are " + Hex(flags) + expected;
    }

    return std::nullopt;
}

std::optional<std::string> CheckDescriptorOutputMode(std::string_view name, uint32_t output_mode)
{
    if (output_mode != kDescriptorOutputStereo)
    {
        return "the " + std::string(name) + " descriptor's output mode is " + std::to_string(output_mode) +
               detail::kOutputModeExpected;
    }

    return std::nullopt;
}

std::optional<std::string> CheckDescriptorHeader(std::string_view name, uint32_t version, uint32_t flags,
                                                 uint32_t output_mode, uint32_t required_flags)
{
    if (auto err = CheckDescriptorVersion(name, version, 1))
    {
        return err;
    }

    if (auto err = CheckDescriptorFlags(name, flags, required_flags))
    {
        return err;
    }

    return CheckDescriptorOutputMode(name, output_mode);
}

void PutTrackVolumes(std::vector<uint8_t>& v, const std::vector<SoundTrackVolumes>& sounds)
{
    Put32(v, static_cast<uint32_t>(sounds.size()));
    for (const SoundTrackVolumes& sound : sounds)
    {
        Put32(v, sound.sound);
        for (const float volume : sound.volumes)
        {
            Put32(v, std::bit_cast<uint32_t>(volume));
        }
    }
}

bool ReadTrackVolumes(const std::vector<uint8_t>& data, std::size_t& pos, std::vector<SoundTrackVolumes>& sounds)
{
    if (pos > data.size() || data.size() - pos < 4)
    {
        return false;
    }

    const uint32_t count = LoadLe32(&data[pos]);
    pos += 4;
    if (count > (data.size() - pos) / kSoundTrackVolumesBytes)
    {
        return false;
    }

    sounds.resize(count);
    for (SoundTrackVolumes& sound : sounds)
    {
        sound.sound = LoadLe32(&data[pos]);
        for (std::size_t t = 0; t < SoundTrackVolumes::kTracks; ++t)
        {
            sound.volumes[t] = std::bit_cast<float>(LoadLe32(&data[pos + 4 + 4 * t]));
        }
        pos += kSoundTrackVolumesBytes;
    }

    return true;
}

std::optional<std::string> CheckTrackVolumes(const std::vector<SoundTrackVolumes>& sounds)
{
    for (std::size_t i = 0; i < sounds.size(); ++i)
    {
        const SoundTrackVolumes& sound = sounds[i];
        const auto same_sound = [&sound](const SoundTrackVolumes& other)
        {
            return other.sound == sound.sound;
        };
        if (std::any_of(sounds.begin() + static_cast<std::ptrdiff_t>(i) + 1, sounds.end(), same_sound))
        {
            return "the archive descriptor lists the track volumes of sound " + Hex(sound.sound, 8) + " more than once";
        }

        const auto volume_ok = [](float volume)
        {
            return std::isfinite(volume) && volume >= 0.0f;
        };
        if (!std::all_of(sound.volumes.begin(), sound.volumes.end(), volume_ok))
        {
            return "a track volume of sound " + Hex(sound.sound, 8) +
                   " in the archive descriptor isn't a finite number of 0 or more";
        }
    }

    return std::nullopt;
}

const SoundTrackVolumes* FindTrackVolumes(const std::vector<SoundTrackVolumes>& sounds, uint32_t sound)
{
    for (const SoundTrackVolumes& entry : sounds)
    {
        if (entry.sound == sound)
        {
            return &entry;
        }
    }

    return nullptr;
}

} // namespace xsflib::psf
