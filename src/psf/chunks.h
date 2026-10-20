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

// The chunks that a program's reader takes besides FILE chunks. It skips the others, and every unknown type.
struct ChunkKinds
{
    bool memory = true; // MEM chunks go into ProgramChunks::memory
    bool sound = true;  // a SND chunk sets ProgramChunks::sound and ProgramChunks::sounds
};

// Splits a decompressed program into its chunks: the FILE chunks, and those that `kinds` takes.
std::optional<std::string> ParseChunks(const std::vector<uint8_t>& program, ProgramChunks& out, ChunkKinds kinds);

} // namespace xsflib::psf::detail
