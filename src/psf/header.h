// SPDX-License-Identifier: MIT

// The fixed header of a PSF file, for the readers of the container and of the streaming programs.

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "xsflib/bytes.h"
#include "xsflib/psf/psf.h"

namespace xsflib::psf::detail
{

// The size of the header: "PSF", the version, and three 32-bit fields.
constexpr std::size_t kHeaderSize = 16;

// A PSF file's header: the version, the sizes of the reserved area and of the compressed program, and the program's
// CRC. `complete` says whether the file holds the reserved area and the program that the sizes give.
struct PsfHeader
{
    uint8_t version = 0;
    uint32_t reserved_size = 0;
    uint32_t program_size = 0;
    uint32_t crc = 0;
    bool complete = false;
};

// The header of `data`, or nothing if `data` isn't a PSF file.
inline std::optional<PsfHeader> ReadPsfHeader(const std::vector<uint8_t>& data)
{
    if (data.size() < kHeaderSize || std::memcmp(data.data(), "PSF", 3) != 0)
    {
        return std::nullopt;
    }

    PsfHeader header;
    header.version = data[3];
    header.reserved_size = LoadLe32(&data[4]);
    header.program_size = LoadLe32(&data[8]);
    header.crc = LoadLe32(&data[12]);
    header.complete = uint64_t{kHeaderSize} + header.reserved_size + header.program_size <= data.size();

    return header;
}

// Checks the header of `data` and returns the error of the first check that fails, from `options`: the signature, the
// version when `options.version` is set, then the sizes. Returns nothing if all pass. `header` gets the header of a
// file with the signature, even when a later check fails.
inline std::optional<std::string> CheckPsfHeader(const std::vector<uint8_t>& data, const ParseOptions& options,
                                                 std::optional<PsfHeader>& header)
{
    header = ReadPsfHeader(data);
    if (!header)
    {
        return options.not_psf;
    }

    if (options.version && header->version != *options.version)
    {
        return options.wrong_version;
    }

    if (!header->complete)
    {
        return options.truncated;
    }

    return std::nullopt;
}

} // namespace xsflib::psf::detail
