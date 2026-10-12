// SPDX-License-Identifier: MIT

// Functions that the rip tools share: writing a file, naming an input, reading its first bytes, the output folder, and
// the folders of a game's sound archives.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace xsflib::rip
{

// Writes `data` to `path`. Returns false if the file can't be written.
bool WriteFile(const std::filesystem::path& path, const std::vector<uint8_t>& data);

// Returns the input's absolute path, without the trailing separator of a folder given as "game/" or ".".
std::filesystem::path InputPath(const std::string& input);

// The name of `input`: the image's or archive's file name without its extension, or the folder's name.
std::string NameOfInput(const std::string& input);

// The first `size` bytes of the file at `path`, or fewer if it's shorter, or nothing if it isn't a file. They
// distinguish a sound archive or a game's executable from a game's files.
std::string ReadHead(const std::string& path, std::size_t size = 8);

// Returns the output folder for `input` when -o isn't given. The folder is next to the input and named after it with
// `suffix` added, such as "_xyz".
std::filesystem::path DefaultOutputDir(const std::string& input, const std::string& suffix);

// Prints the rip's output folder as the last line about the input.
void ReportOutput(const std::filesystem::path& out_dir);

// The folders that the sets of a game's sound archives go in, given the archives' paths in the game's files. Each
// folder's name is the archive's name without its extension. Where two archives' names are the same, ignoring case, the
// folders above each archive are added until the folders' names differ (content/BGM/sound.xyz and content/SE/sound.xyz
// go in BGM_sound and SE_sound).
std::vector<std::string> ArchiveFolders(const std::vector<std::string>& paths);

} // namespace xsflib::rip
