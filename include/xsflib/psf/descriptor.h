// SPDX-License-Identifier: MIT

// The checks of a format's descriptors: the version, the flags and the output mode. A descriptor describes how a player
// plays a set, such as a process descriptor for an emulated program or an archive descriptor for a sound archive. Each
// message names the descriptor by `name`, such as "process" or "archive". A descriptor's paths are written and read
// with PutPath, PadTo4 and ReadPath (see streaming.h). The fields that every format's archive descriptor has are
// written, read and checked with the templates at the end.

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "xsflib/bytes.h"
#include "xsflib/psf/psf.h"
#include "xsflib/psf/streaming.h"

namespace xsflib::psf
{

// The flag "DSP required" (bit 0). Some formats' version 1 descriptors require it.
inline constexpr uint32_t kDescriptorFlagDspRequired = 1;

// The output mode that version 1 plays: stereo (1).
inline constexpr uint32_t kDescriptorOutputStereo = 1;

// Why a player that supports version `supported` can't take a descriptor of `version`, or nothing.
std::optional<std::string> CheckDescriptorVersion(std::string_view name, uint32_t version, uint32_t supported);

// Why a version 1 descriptor's flags aren't `required`, the flags that version 1 requires, or nothing. `required` is 0
// for no flags, or kDescriptorFlagDspRequired.
std::optional<std::string> CheckDescriptorFlags(std::string_view name, uint32_t flags, uint32_t required);

// Why version 1 can't play a descriptor's output mode, or nothing. Version 1 plays stereo only.
std::optional<std::string> CheckDescriptorOutputMode(std::string_view name, uint32_t output_mode);

// The checks of a version 1 descriptor's version, flags and output mode, in that order. Returns the first failure, or
// nothing. The checks of the flags and the output mode follow version 1. A player that supports a later version calls
// CheckDescriptorVersion with that version, and checks the flags and the output mode itself.
std::optional<std::string> CheckDescriptorHeader(std::string_view name, uint32_t version, uint32_t flags,
                                                 uint32_t output_mode, uint32_t required_flags);

// The fields that every format's archive descriptor has. The descriptor starts with the format's magic, then the
// version, the flags, the output mode and the voice count, each a 32-bit little-endian word. It ends with the sound
// archive's path, the count of the group files (16 bits), their paths, and zeros to a multiple of four bytes. The
// format's other fields lie between the two. `Descriptor` is the format's archive descriptor: its magic is kMagic, and
// its fields are version, flags, output_mode, voice_count, archive_path and group_paths.

// Writes the start of `d`: the magic, the version, the flags, the output mode and the voice count.
template <typename Descriptor>
void PutArchiveStart(std::vector<uint8_t>& v, const Descriptor& d)
{
    Put32(v, Descriptor::kMagic);
    Put32(v, d.version);
    Put32(v, d.flags);
    Put32(v, d.output_mode);
    Put32(v, d.voice_count);
}

// Reads the start into `d`. `size` is the bytes of the words before the paths: 20 for the start, and the format's
// words. False if `data` is shorter, or if its magic isn't Descriptor::kMagic.
template <typename Descriptor>
bool ReadArchiveStart(const std::vector<uint8_t>& data, std::size_t size, Descriptor& d)
{
    if (data.size() < std::max<std::size_t>(size, 20) || LoadLe32(data.data()) != Descriptor::kMagic)
    {
        return false;
    }

    d.version = LoadLe32(&data[4]);
    d.flags = LoadLe32(&data[8]);
    d.output_mode = LoadLe32(&data[12]);
    d.voice_count = LoadLe32(&data[16]);

    return true;
}

// Writes the end of `d`: the archive's path, the group files' count and paths, and the zeros.
template <typename Descriptor>
void PutArchivePaths(std::vector<uint8_t>& v, const Descriptor& d)
{
    PutPath(v, d.archive_path);
    Put16(v, static_cast<uint32_t>(d.group_paths.size()));
    for (const std::string& path : d.group_paths)
    {
        PutPath(v, path);
    }

    PadTo4(v);
}

// Reads the end at `pos` into `d`, and moves `pos` past the last path. False if `data` ends first.
template <typename Descriptor>
bool ReadArchivePaths(const std::vector<uint8_t>& data, std::size_t& pos, Descriptor& d)
{
    const std::optional<std::string> archive = ReadPath(data, pos);
    const std::optional<uint32_t> groups = archive ? Read16(data, pos) : std::nullopt;
    if (!groups)
    {
        return false;
    }

    d.archive_path = *archive;
    for (uint32_t i = 0; i < *groups; ++i)
    {
        const std::optional<std::string> path = ReadPath(data, pos);
        if (!path)
        {
            return false;
        }

        d.group_paths.push_back(*path);
    }

    return true;
}

// The checks of a version 1 archive descriptor's start: CheckDescriptorHeader with no flags, then the voice count, 1 to
// `max_voices`. Returns the first failure, or nothing.
template <typename Descriptor>
std::optional<std::string> CheckArchiveStart(const Descriptor& d, uint32_t max_voices)
{
    if (auto err = CheckDescriptorHeader("archive", d.version, d.flags, d.output_mode, 0))
    {
        return err;
    }

    if (d.voice_count < 1 || d.voice_count > max_voices)
    {
        return "the archive descriptor's voice count is " + std::to_string(d.voice_count) + "; it must be 1 to " +
               std::to_string(max_voices);
    }

    return std::nullopt;
}

} // namespace xsflib::psf
