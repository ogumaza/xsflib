// SPDX-License-Identifier: MIT

// The ROM sets of arcade boards (see rom_set.h).

#include "xsflib/rip/rom_set.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "xsflib/files.h"
#include "xsflib/rip/zip.h"
#include "xsflib/sha1.h"

namespace xsflib::rip
{

namespace
{

namespace fs = std::filesystem;

// A chip's identity: its size and SHA-1.
using ChipKey = std::pair<uint64_t, std::string>;

// An ASCII letter in lower case, and any other character as it is.
char AsciiLower(char c)
{
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

// Whether `name` ends in `extension`, with ASCII letters in any case.
bool EndsInExtension(std::string_view name, std::string_view extension)
{
    if (name.size() < extension.size())
    {
        return false;
    }

    const std::string_view end = name.substr(name.size() - extension.size());
    const auto same = [](char a, char b)
    {
        return AsciiLower(a) == AsciiLower(b);
    };
    return std::equal(end.begin(), end.end(), extension.begin(), extension.end(), same);
}

} // namespace

bool IsRomSet(const fs::path& path)
{
    std::error_code error;
    if (fs::is_directory(path, error))
    {
        return true;
    }

    std::ifstream in(path, std::ios::binary);
    std::array<uint8_t, 4> head{};
    in.read(reinterpret_cast<char*>(head.data()), head.size());

    return in.gcount() == static_cast<std::streamsize>(head.size()) && LooksLikeZip(head);
}

std::optional<std::string> ReadRomSet(const fs::path& path, const std::set<uint64_t>& sizes, std::string_view program,
                                      std::vector<SetFile>& files)
{
    std::error_code error;
    if (!fs::is_directory(path, error))
    {
        std::vector<uint8_t> zip;
        if (!ReadFile(path, zip))
        {
            return "can't read " + Utf8(path);
        }

        std::vector<ZipEntry> entries;
        if (auto problem = ReadZip(zip, entries, program))
        {
            return Utf8(path) + ": " + *problem;
        }

        for (ZipEntry& entry : entries)
        {
            files.push_back({std::move(entry.name), std::move(entry.data)});
        }

        return std::nullopt;
    }

    // A folder may hold more than the set. Only files of a chip's size are read.
    for (const fs::directory_entry& entry : fs::directory_iterator(path, error))
    {
        std::error_code entry_error;
        if (!entry.is_regular_file(entry_error) || sizes.count(entry.file_size(entry_error)) == 0)
        {
            continue;
        }

        SetFile file;
        file.name = Utf8(entry.path().filename());
        if (!ReadFile(entry.path(), file.data))
        {
            return "can't read " + Utf8(entry.path());
        }
        files.push_back(std::move(file));
    }
    if (error)
    {
        return "can't list " + Utf8(path) + ": " + error.message();
    }

    return std::nullopt;
}

RomSetImage BuildRomSetImage(std::span<const RomSetGame> games, const std::vector<SetFile>& files)
{
    std::map<ChipKey, const SetFile*> found;
    for (const SetFile& file : files)
    {
        found.emplace(ChipKey{file.data.size(), Sha1::Hex(Sha1::Of(file.data))}, &file);
    }

    // The game whose chips are all here. Failing that, the one with the most of its chips here names what's missing.
    RomSetImage result;
    std::size_t best = 0;
    for (std::size_t index = 0; index < games.size(); index++)
    {
        const RomSetGame& game = games[index];
        if (game.chips.empty())
        {
            continue;
        }

        std::vector<const RomChip*> missing;
        for (const RomChip& chip : game.chips)
        {
            if (found.count(ChipKey{chip.size, chip.sha1}) == 0)
            {
                missing.push_back(&chip);
            }
        }

        if (missing.empty())
        {
            uint32_t size = 0;
            for (const RomChip& chip : game.chips)
            {
                size = std::max(size, chip.offset + chip.size);
            }

            result.game = index;
            result.rom.assign(size, 0);
            for (const RomChip& chip : game.chips)
            {
                const std::vector<uint8_t>& data = found.at(ChipKey{chip.size, chip.sha1})->data;
                uint8_t* out = result.rom.data() + chip.offset;
                std::copy(data.begin(), data.end(), out);

                if (chip.byte_swapped)
                {
                    for (uint32_t i = 0; i + 1 < chip.size; i += 2)
                    {
                        std::swap(out[i], out[i + 1]);
                    }
                }
            }
            return result;
        }

        const std::size_t present = game.chips.size() - missing.size();
        if (present > best)
        {
            best = present;
            result.problem = "the set of " + std::string(game.name) + " is missing " + std::to_string(missing.size()) +
                             " of its " + std::to_string(game.chips.size()) + " chips:";
            for (const RomChip* chip : missing)
            {
                result.problem += " " + std::to_string(chip->size) + " bytes with SHA-1 " + chip->sha1 + ";";
            }
            result.problem.pop_back();
        }
    }

    if (result.problem.empty())
    {
        result.problem = "no profile's chips are in the set";
    }

    return result;
}

SetFile* OnlyFileWithExtension(std::vector<SetFile>& files, std::span<const std::string_view> extensions)
{
    SetFile* found = nullptr;
    for (SetFile& file : files)
    {
        const auto matches = [&](std::string_view extension)
        {
            return EndsInExtension(file.name, extension);
        };
        if (std::none_of(extensions.begin(), extensions.end(), matches))
        {
            continue;
        }

        if (found != nullptr)
        {
            return nullptr;
        }

        found = &file;
    }

    return found;
}

} // namespace xsflib::rip
