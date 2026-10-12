// SPDX-License-Identifier: MIT

// The fixed header of a PSF file, for the readers of the container and of the streaming programs.

#pragma once

#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

#include "xsflib/bytes.h"

namespace xsflib::psf::detail
{

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
    if (data.size() < 16 || std::memcmp(data.data(), "PSF", 3) != 0)
    {
        return std::nullopt;
    }

    PsfHeader header;
    header.version = data[3];
    header.reserved_size = LoadLe32(&data[4]);
    header.program_size = LoadLe32(&data[8]);
    header.crc = LoadLe32(&data[12]);
    header.complete = 16ull + header.reserved_size + header.program_size <= data.size();

    return header;
}

} // namespace xsflib::psf::detail
