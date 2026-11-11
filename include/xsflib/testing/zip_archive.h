// SPDX-License-Identifier: MIT

// ZIP archives that a test builds in memory, in the layout of PKWARE's APPNOTE.TXT: each entry's local header and
// bytes, then the central directory and its end record. An entry is stored, or deflated by the test beforehand. The
// archives have no times, no extra fields, no comments and no ZIP64 records.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "xsflib/bytes.h"

namespace xsflib::testing
{

// The compression methods of ZIP entries.
inline constexpr uint16_t kZipStored = 0;
inline constexpr uint16_t kZipDeflated = 8;

// An entry of a ZIP archive.
struct ZipArchiveEntry
{
    std::string name;
    uint16_t method = kZipStored;
    std::vector<uint8_t> stored; // the bytes in the archive
    std::vector<uint8_t> data;   // the bytes once read. The headers give their CRC-32 and size.
};

namespace detail
{

inline constexpr uint32_t kZipLocalHeader = 0x04034B50;
inline constexpr uint32_t kZipCentralHeader = 0x02014B50;
inline constexpr uint32_t kZipEndOfCentralDirectory = 0x06054B50;

// The version of the format that the archives require and are made by: 2.0, for deflated entries.
inline constexpr uint16_t kZipVersion = 20;

// The polynomial of CRC-32, with its bits reversed.
inline constexpr uint32_t kCrc32Polynomial = 0xEDB88320;

} // namespace detail

// The CRC-32 of `data` that ZIP archives and zlib use.
inline uint32_t ZipCrc32(std::span<const uint8_t> data)
{
    uint32_t crc = 0xFFFFFFFF;
    for (const uint8_t byte : data)
    {
        crc ^= byte;
        for (int bit = 0; bit < 8; bit++)
        {
            crc = (crc >> 1) ^ ((crc & 1) != 0 ? detail::kCrc32Polynomial : 0);
        }
    }

    return ~crc;
}

// A stored entry: its bytes in the archive are `data`.
inline ZipArchiveEntry StoredEntry(std::string name, const std::vector<uint8_t>& data)
{
    return {std::move(name), kZipStored, data, data};
}

// A deflated entry. `deflated` is a raw deflate stream of `data`.
inline ZipArchiveEntry DeflatedEntry(std::string name, std::vector<uint8_t> deflated, std::vector<uint8_t> data)
{
    return {std::move(name), kZipDeflated, std::move(deflated), std::move(data)};
}

// An archive of `entries`, in their order.
inline std::vector<uint8_t> ZipArchive(const std::vector<ZipArchiveEntry>& entries)
{
    // The fields that an entry's local header and its record in the central directory share, from the method to the
    // name's length. Neither has a time.
    const auto append_fields = [](std::vector<uint8_t>& out, const ZipArchiveEntry& entry)
    {
        AppendLe16(out, entry.method);
        AppendLe32(out, 0);
        AppendLe32(out, ZipCrc32(entry.data));
        AppendLe32(out, static_cast<uint32_t>(entry.stored.size()));
        AppendLe32(out, static_cast<uint32_t>(entry.data.size()));
        AppendLe16(out, static_cast<uint16_t>(entry.name.size()));
    };

    // Each entry's local header, with no flags and no extra field, then its name and its bytes.
    std::vector<uint8_t> zip;
    std::vector<uint32_t> offsets;
    for (const ZipArchiveEntry& entry : entries)
    {
        offsets.push_back(static_cast<uint32_t>(zip.size()));
        AppendLe32(zip, detail::kZipLocalHeader);
        AppendLe16(zip, detail::kZipVersion);
        AppendLe16(zip, 0);
        append_fields(zip, entry);
        AppendLe16(zip, 0);
        zip.insert(zip.end(), entry.name.begin(), entry.name.end());
        zip.insert(zip.end(), entry.stored.begin(), entry.stored.end());
    }

    // The central directory: each entry's record, with no extra field, comment or attributes, and the offset of its
    // local header.
    const auto directory = static_cast<uint32_t>(zip.size());
    for (std::size_t i = 0; i < entries.size(); i++)
    {
        AppendLe32(zip, detail::kZipCentralHeader);
        AppendLe16(zip, detail::kZipVersion);
        AppendLe16(zip, detail::kZipVersion);
        AppendLe16(zip, 0);
        append_fields(zip, entries[i]);
        AppendLe16(zip, 0);
        AppendLe16(zip, 0);
        AppendLe16(zip, 0);
        AppendLe16(zip, 0);
        AppendLe32(zip, 0);
        AppendLe32(zip, offsets[i]);
        zip.insert(zip.end(), entries[i].name.begin(), entries[i].name.end());
    }

    // The end record: one disk, the count of the entries, the directory's size and offset, and no comment.
    const auto directory_size = static_cast<uint32_t>(zip.size()) - directory;
    AppendLe32(zip, detail::kZipEndOfCentralDirectory);
    AppendLe32(zip, 0);
    AppendLe16(zip, static_cast<uint16_t>(entries.size()));
    AppendLe16(zip, static_cast<uint16_t>(entries.size()));
    AppendLe32(zip, directory_size);
    AppendLe32(zip, directory);
    AppendLe16(zip, 0);

    return zip;
}

} // namespace xsflib::testing
