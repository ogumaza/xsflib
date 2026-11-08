// SPDX-License-Identifier: MIT

// The ROM sets of arcade boards. The user gives a set as a ZIP archive or as a folder of its chips. A game's chips are
// found by their SHA-1 and size, whatever the files are called, and put together into the image that the board's CPU
// sees.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace xsflib::rip
{

// A file of a set.
struct SetFile
{
    std::string name;
    std::vector<uint8_t> data;
};

// One chip of a game's ROM set.
struct RomChip
{
    std::string sha1;          // Of the chip as the set holds it, in lower-case hex.
    uint32_t size = 0;         // In bytes.
    uint32_t offset = 0;       // Where its first byte goes in the image.
    bool byte_swapped = false; // Each pair of bytes is stored in reverse.
};

// A game that a set may hold: its name in messages, and its chips. A game with no chips never matches.
struct RomSetGame
{
    std::string_view name;
    std::span<const RomChip> chips;
};

// Whether `path` may be a ROM set: a folder or a ZIP archive.
bool IsRomSet(const std::filesystem::path& path);

// Reads the files of the set at `path`: the entries of a ZIP archive, or the files of a folder (not of its subfolders)
// whose sizes are among `sizes`. `program` names the reader in the message about a compression method that it doesn't
// read. Returns why it can't, or nothing.
std::optional<std::string> ReadRomSet(const std::filesystem::path& path, const std::set<uint64_t>& sizes,
                                      std::string_view program, std::vector<SetFile>& files);

struct RomSetImage
{
    std::optional<std::size_t> game; // The index of the game whose chips are all in the set.
    std::vector<uint8_t> rom;        // Its image, with each chip at its offset and each pair of bytes in order.
    std::string problem;             // Without a game: why.
};

// Puts together the image of the first game whose chips are all among `files`. Failing that, the problem names the
// chips that are missing from the game with the most of its chips in the set.
RomSetImage BuildRomSetImage(std::span<const RomSetGame> games, const std::vector<SetFile>& files);

// The one file among `files` whose name ends in one of `extensions`, such as ".xyz", in any case of ASCII letters.
// Returns null when no file's name does, or more than one's does. An archive that holds no game's chips may hold an
// image in another form, such as a cartridge's.
SetFile* OnlyFileWithExtension(std::vector<SetFile>& files, std::span<const std::string_view> extensions);

} // namespace xsflib::rip
