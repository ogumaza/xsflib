// SPDX-License-Identifier: MIT

// A reader of ZIP archives, for ROM sets. It reads stored and deflated entries, and checks each entry's CRC-32. It
// doesn't read ZIP64 archives, encrypted entries or archives that span several files.

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace xsflib::rip
{

struct ZipEntry
{
    std::string name;
    std::vector<uint8_t> data;
};

// Whether `bytes` start as a ZIP archive does.
bool LooksLikeZip(std::span<const uint8_t> bytes);

// Reads every file of the archive `zip` into `entries`, and skips folders. Returns why it can't, or nothing. `program`
// names the reader in the message about a compression method that it doesn't read.
std::optional<std::string> ReadZip(std::span<const uint8_t> zip, std::vector<ZipEntry>& entries,
                                   std::string_view program);

} // namespace xsflib::rip
