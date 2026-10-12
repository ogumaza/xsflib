// SPDX-License-Identifier: MIT

// The inflater of a PSF file's program. The container's decompressor and the streaming reader share it.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace xsflib::psf::detail
{

// Inflates the compressed program `compressed` of `size` bytes onto the end of `program`. Returns the streaming
// reader's error for a stream that's damaged, cut short or larger than 1 GiB, or nothing.
std::optional<std::string> InflateWhole(const uint8_t* compressed, uint32_t size, std::vector<uint8_t>& program);

} // namespace xsflib::psf::detail
