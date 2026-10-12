// SPDX-License-Identifier: MIT

// zlib's part of the PSF container (see psf.h): the default decompressor, and the writer.

#include "xsflib/psf/psf.h"

#include <zlib.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "inflate.h"
#include "xsflib/psf/streaming.h"

namespace xsflib::psf
{

std::optional<std::string> InflateProgram(const uint8_t* data, std::size_t size, uint32_t crc,
                                          std::vector<uint8_t>& program)
{
    if (crc32(0, data, static_cast<uInt>(size)) != crc)
    {
        return "program CRC mismatch";
    }

    return detail::InflateWhole(data, static_cast<uint32_t>(size), program);
}

std::optional<std::string> ParsePsf(const std::vector<uint8_t>& data, PsfFile& out, bool with_program)
{
    ParseOptions options;
    options.with_program = with_program;
    options.decompress = InflateProgram;

    return ParsePsf(data, out, options);
}

std::vector<uint8_t> WritePsf(uint8_t version, const std::vector<uint8_t>& reserved,
                              const std::vector<uint8_t>& program, const Tags& tags)
{
    return WritePsfPieces(version, reserved, {std::span<const uint8_t>(program)}, tags);
}

} // namespace xsflib::psf
