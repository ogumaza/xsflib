// SPDX-License-Identifier: MIT

// The program of a format that copies a set's files into it (see program.h).

#include "xsflib/psf/program.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "chunks.h"
#include "output_mode.h"
#include "xsflib/psf/psf.h"
#include "xsflib/psf/streaming.h"

namespace xsflib::psf
{

void ProgramBuilder::Chunk(const char type[4], const std::vector<uint8_t>& payload)
{
    program_.insert(program_.end(), type, type + 4);
    Put32(program_, static_cast<uint32_t>(payload.size()));
    program_.insert(program_.end(), payload.begin(), payload.end());
    PadTo4(program_);
}

void ProgramBuilder::AddMemory(uint32_t address, const void* data, std::size_t size)
{
    std::vector<uint8_t> payload;
    Put32(payload, address);
    const auto* p = static_cast<const uint8_t*>(data);
    payload.insert(payload.end(), p, p + size);
    Chunk("MEM ", payload);
}

void ProgramBuilder::AddFile(const std::string& path, const std::vector<uint8_t>& data)
{
    std::vector<uint8_t> payload;
    PutPath(payload, path);
    PadTo4(payload);
    payload.insert(payload.end(), data.begin(), data.end());
    Chunk("FILE", payload);
}

void ProgramBuilder::AddSound(uint32_t sound_id)
{
    const uint32_t sound_ids[] = {sound_id};
    AddSounds(sound_ids);
}

void ProgramBuilder::AddSounds(std::span<const uint32_t> sound_ids)
{
    if (sound_ids.empty())
    {
        throw std::invalid_argument("a SND chunk selects at least one sound");
    }

    std::vector<uint8_t> payload;
    for (const uint32_t sound_id : sound_ids)
    {
        Put32(payload, sound_id);
    }

    Chunk("SND ", payload);
}

std::optional<std::string> ParseProgram(const std::vector<uint8_t>& program, ProgramChunks& out,
                                        const ProgramOptions& options)
{
    return detail::ParseChunks(program, out, {.memory = true, .sound = options.sound_chunks});
}

const std::vector<uint8_t>* FindFile(const std::vector<FileChunk>& files, const std::string& path)
{
    const std::vector<uint8_t>* found = nullptr;
    for (const auto& f : files)
    {
        if (f.path == path)
        {
            found = &f.data; // later chunks replace earlier ones
        }
    }

    return found;
}

std::optional<uint8_t> LoadedByte(const std::vector<MemChunk>& memory, uint32_t address)
{
    for (auto it = memory.rbegin(); it != memory.rend(); ++it)
    {
        if (address >= it->address && address - it->address < it->data.size())
        {
            return it->data[address - it->address];
        }
    }

    return std::nullopt;
}

std::optional<uint32_t> LoadedWord(const std::vector<MemChunk>& memory, uint32_t address, bool big_endian)
{
    uint32_t value = 0;
    for (uint32_t i = 0; i < 4; i++)
    {
        const std::optional<uint8_t> byte = LoadedByte(memory, address + i);
        if (!byte)
        {
            return std::nullopt;
        }

        if (big_endian)
        {
            value = value << 8 | *byte;
        }
        else
        {
            value |= uint32_t{*byte} << (8 * i);
        }
    }

    return value;
}

std::optional<std::string> CheckDriverBlock(const std::vector<MemChunk>& memory, const DriverBlockLayout& layout)
{
    if (LoadedWord(memory, layout.address, layout.big_endian) != layout.magic)
    {
        return std::nullopt;
    }

    const std::optional<uint32_t> version =
        LoadedWord(memory, layout.address + layout.version_offset, layout.big_endian);
    std::optional<uint32_t> mode;
    if (layout.output_mode_offset)
    {
        mode = LoadedWord(memory, layout.address + *layout.output_mode_offset, layout.big_endian);
    }

    if (!version || (layout.output_mode_offset && !mode))
    {
        return "the driver block is cut short";
    }

    const uint32_t newest = std::max(layout.version, layout.newest_version);
    if (*version < layout.version || *version > newest)
    {
        const std::string supported =
            newest == layout.version ? "version " + std::to_string(newest)
                                     : "versions " + std::to_string(layout.version) + " to " + std::to_string(newest);
        return "driver block version " + std::to_string(*version) + "; this player supports " + supported;
    }

    if (layout.output_mode_offset && *mode != layout.output_mode)
    {
        return "the driver block's output mode is " + std::to_string(*mode) + detail::kOutputModeExpected;
    }

    return std::nullopt;
}

} // namespace xsflib::psf
