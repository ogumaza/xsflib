// SPDX-License-Identifier: MIT

// The inflater of a PSF file's program, and the check of its CRC. The container's decompressor and the streaming reader
// share them.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace xsflib::psf::detail
{

// Inflates the compressed program `compressed` of `size` bytes onto the end of `program`. Returns the streaming
// reader's error for a stream that's damaged, cut short or larger than 1 GiB, or nothing.
std::optional<std::string> InflateWhole(const uint8_t* compressed, uint32_t size, std::vector<uint8_t>& program);

// Returns the error for a compressed program `compressed` of `size` bytes whose CRC-32 isn't `crc`, or nothing.
std::optional<std::string> CheckProgramCrc(const uint8_t* compressed, std::size_t size, uint32_t crc);

} // namespace xsflib::psf::detail
