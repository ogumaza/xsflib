// SPDX-License-Identifier: MIT

// The program of a format that copies a set's files into it: MEM, FILE and SND chunks. Each chunk is padded to a
// multiple of four bytes. Also what the formats read from a set's memory: their drivers' parameter block.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace xsflib::psf
{

// A MEM chunk: data to load at `address` in the emulated memory.
struct MemChunk
{
    uint32_t address;
    std::vector<uint8_t> data;
};

// A FILE chunk: a file in the set, such as the sound archive or the DSP firmware.
struct FileChunk
{
    std::string path;
    std::vector<uint8_t> data;
};

// Builds a program: the chunks in the order they're added, each padded to a multiple of four bytes.
class ProgramBuilder
{
public:
    void AddMemory(uint32_t address, const void* data, std::size_t size);

    void AddMemory(uint32_t address, const std::vector<uint8_t>& data)
    {
        AddMemory(address, data.data(), data.size());
    }

    void AddFile(const std::string& path, const std::vector<uint8_t>& data);

    // Selects the sound to play in archive mode (`SND ` chunk): its sound's item id or index.
    void AddSound(uint32_t sound_id);

    const std::vector<uint8_t>& Data() const
    {
        return program_;
    }

private:
    void Chunk(const char type[4], const std::vector<uint8_t>& payload);

    std::vector<uint8_t> program_;
};

// A decompressed program split into chunks.
struct ProgramChunks
{
    std::vector<MemChunk> memory;
    std::vector<FileChunk> files;
    std::optional<uint32_t> sound; // the last `SND ` chunk
};

// How ParseProgram reads a program.
struct ProgramOptions
{
    // Whether SND chunks are read. Without this, they're skipped like any unknown type. A format without archive mode
    // skips them.
    bool sound_chunks = true;
};

// Splits a decompressed program into chunks (unknown types are skipped).
std::optional<std::string> ParseProgram(const std::vector<uint8_t>& program, ProgramChunks& out,
                                        const ProgramOptions& options = {});

// The data of the file at `path` among a set's FILE chunks, or nullptr if there's none. A later chunk replaces an
// earlier one. Loading the set follows the same rule.
const std::vector<uint8_t>* FindFile(const std::vector<FileChunk>& files, const std::string& path);

// The byte that a set's MEM chunks leave at `address`, or nothing if none of them covers it. A later chunk overwrites
// an earlier one, as in loading.
std::optional<uint8_t> LoadedByte(const std::vector<MemChunk>& memory, uint32_t address);

// The word that a set's MEM chunks leave at `address`, in the guest's byte order.
std::optional<uint32_t> LoadedWord(const std::vector<MemChunk>& memory, uint32_t address, bool big_endian);

// Where a format's game-mode driver keeps its parameter block, and what a player of the format's version 1 accepts.
struct DriverBlockLayout
{
    uint32_t address = 0;
    uint32_t magic = 0;
    uint32_t version = 1;

    // The newest block version that the player accepts. A format whose drivers have several versions sets it. Versions
    // from `version` to it pass. 0 accepts `version` alone.
    uint32_t newest_version = 0;

    uint32_t version_offset = 0x04;
    bool big_endian = false;

    // The offset of the block's output mode, for a format whose version 1 plays one output mode, and that mode.
    std::optional<uint32_t> output_mode_offset = std::nullopt;
    uint32_t output_mode = 0;
};

// Returns why a version 1 player can't play a game-mode set with this driver block, or nothing if it can. A set without
// the block (no magic at its address) passes: it doesn't use the format's driver.
std::optional<std::string> CheckDriverBlock(const std::vector<MemChunk>& memory, const DriverBlockLayout& layout);

// Diagnostic suffixes for unsupported descriptor flags and output modes, in the formats whose version 1 requires the
// flag "DSP required" (bit 0) and plays stereo only.
const char* FlagsExpected();
const char* OutputModeExpected();

} // namespace xsflib::psf
