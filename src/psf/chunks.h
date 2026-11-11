// SPDX-License-Identifier: MIT

// The chunk reader and the chunk writers that the programs of both kinds of format share (see program.h and
// streaming.h).

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
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

// A FILE chunk's header and its path, padded to four bytes. The file's `data_size` bytes of data follow them.
std::vector<uint8_t> FileChunkHead(const std::string& path, std::size_t data_size);

// A whole FILE chunk: its header, its path and `data`, padded to four bytes.
std::vector<uint8_t> FileChunkBytes(const std::string& path, std::span<const uint8_t> data);

// A whole SND chunk with `sound_ids`. Throws std::invalid_argument if `sound_ids` is empty.
std::vector<uint8_t> SoundChunkBytes(std::span<const uint32_t> sound_ids);

} // namespace xsflib::psf::detail
