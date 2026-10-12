// SPDX-License-Identifier: MIT

// Files and their paths: UTF-8 text for a path on every platform, and reading and writing whole files.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <istream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace xsflib
{

// A path from UTF-8 text. A Windows path from narrow text would take the ANSI code page instead, and lose the
// characters outside it.
std::filesystem::path PathFromUtf8(const std::string& s);

// A path as UTF-8 text.
std::string Utf8(const std::filesystem::path& p);

// Reads all of `in`, a stream just opened, into `data`. A stream of a known size is read at once. Others, such as
// pipes, are read up to their end. Returns false if the size is over `max_size`.
bool ReadStream(std::istream& in, std::vector<uint8_t>& data,
                std::size_t max_size = std::numeric_limits<std::size_t>::max());

// Reads the whole file at `path` into `data` with ReadStream. Returns false if it can't be opened, or if it's larger
// than `max_size`.
bool ReadFile(const std::filesystem::path& path, std::vector<uint8_t>& data,
              std::size_t max_size = std::numeric_limits<std::size_t>::max());

// Writes `data` to the file at `path`, replacing it. Returns the error ("can't write" and the path), or nothing. The
// check comes after the file is closed. Closing writes the last bytes, and can find a full disk.
std::optional<std::string> WriteFile(const std::filesystem::path& path, std::span<const uint8_t> data);

} // namespace xsflib
