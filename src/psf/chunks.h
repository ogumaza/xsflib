// SPDX-License-Identifier: MIT

// The chunk reader that the programs of both kinds of format share (see program.h and streaming.h).

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "xsflib/psf/program.h"

namespace xsflib::psf::detail
{

// Splits a decompressed program into its chunks. With `memory_chunks`, MEM chunks go into `out.memory`. With
// `sound_chunks`, a SND chunk sets `out.sound`. Other chunks, and those that the switches leave out, are skipped.
std::optional<std::string> ParseChunks(const std::vector<uint8_t>& program, ProgramChunks& out, bool memory_chunks,
                                       bool sound_chunks);

} // namespace xsflib::psf::detail
