// SPDX-License-Identifier: MIT

// The reader of ZIP archives (see zip.h). The layout comes from PKWARE's APPNOTE.TXT: the end of central directory
// record, the central directory's file headers and the local file headers, all little-endian.

#include "xsflib/rip/zip.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <zlib.h>

#include "xsflib/bytes.h"

namespace xsflib::rip
{
namespace
{

constexpr uint32_t kLocalHeader = 0x04034B50;
constexpr uint32_t kCentralHeader = 0x02014B50;
constexpr uint32_t kEndOfCentralDirectory = 0x06054B50;

constexpr std::size_t kLocalHeaderSize = 30;
constexpr std::size_t kCentralHeaderSize = 46;
constexpr std::size_t kEndSize = 22;
constexpr std::size_t kMaxCommentSize = 0xFFFF;

// The compression methods that the reader reads.
constexpr uint16_t kStored = 0;
constexpr uint16_t kDeflated = 8;

// The bit of an entry's flags that marks it encrypted.
constexpr uint16_t kEncryptedFlag = 1;

// No entry of a ROM set comes near this. It keeps a damaged size from asking for gigabytes.
constexpr uint32_t kMaxEntrySize = 512u << 20;

std::optional<std::string> Inflate(std::span<const uint8_t> compressed, std::vector<uint8_t>& out)
{
    z_stream stream{};
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
    {
        return "zlib can't start";
    }

    // zlib refuses a null output buffer, and an empty vector may have one. An empty entry gets a byte that it never
    // fills.
    Bytef unused = 0;
    stream.next_in = const_cast<Bytef*>(compressed.data());
    stream.avail_in = static_cast<uInt>(compressed.size());
    stream.next_out = out.empty() ? &unused : out.data();
    stream.avail_out = static_cast<uInt>(out.size());
    const int result = inflate(&stream, Z_FINISH);
    const bool complete = result == Z_STREAM_END && stream.total_out == out.size();
    inflateEnd(&stream);

    if (!complete)
    {
        return "a deflated entry is damaged";
    }

    return std::nullopt;
}

// Where the central directory is, from the end record.
struct Directory
{
    std::size_t end = 0; // the offset of the end record
    uint16_t count = 0;  // the entries
    uint32_t offset = 0; // the offset of the directory
};

// Finds the end record of `zip` and its central directory. Returns why it can't, or nothing.
std::optional<std::string> FindDirectory(std::span<const uint8_t> zip, Directory& directory)
{
    // The end record is the last thing in the archive, before a comment of up to 64 KiB.
    if (zip.size() < kEndSize)
    {
        return "the archive is too short";
    }

    std::size_t end = zip.size() - kEndSize;
    const std::size_t lowest = zip.size() - kEndSize > kMaxCommentSize ? zip.size() - kEndSize - kMaxCommentSize : 0;
    while (LoadLe32(zip.data() + end) != kEndOfCentralDirectory)
    {
        if (end == lowest)
        {
            return "the archive has no end record";
        }

        end--;
    }

    const uint8_t* record = zip.data() + end;
    const uint16_t count = LoadLe16(record + 10);
    const uint32_t directory_size = LoadLe32(record + 12);
    const uint32_t directory_offset = LoadLe32(record + 16);
    if (LoadLe16(record + 4) != 0 || LoadLe16(record + 6) != 0)
    {
        return "the archive spans several files";
    }

    if (directory_offset == 0xFFFFFFFF || count == 0xFFFF)
    {
        return "the archive is in the ZIP64 format";
    }

    if (uint64_t{directory_offset} + directory_size > end)
    {
        return "the archive's directory runs past its end";
    }

    directory = {end, count, directory_offset};

    return std::nullopt;
}

// The fields of a central directory header that the data of its entry requires.
struct CentralEntry
{
    uint16_t flags = 0;
    uint16_t method = 0;
    uint32_t crc = 0;
    uint32_t compressed_size = 0;
    uint32_t size = 0;
    uint32_t local_offset = 0;
};

// Reads the data of the entry that `central` describes into `entry.data`, and checks it against the CRC-32. `program`
// names the reader in the message about a compression method that it doesn't read. Returns why it can't, or nothing.
std::optional<std::string> ReadEntryData(std::span<const uint8_t> zip, const CentralEntry& central,
                                         std::string_view program, ZipEntry& entry)
{
    if ((central.flags & kEncryptedFlag) != 0)
    {
        return entry.name + " is encrypted";
    }

    if (central.size > kMaxEntrySize)
    {
        return entry.name + " is too large";
    }

    if (central.local_offset + uint64_t{kLocalHeaderSize} > zip.size() ||
        LoadLe32(zip.data() + central.local_offset) != kLocalHeader)
    {
        return entry.name + ": the archive is damaged";
    }

    const uint8_t* local = zip.data() + central.local_offset;
    const uint64_t data_offset =
        uint64_t{central.local_offset} + kLocalHeaderSize + LoadLe16(local + 26) + LoadLe16(local + 28);
    if (data_offset + central.compressed_size > zip.size())
    {
        return entry.name + ": the archive is cut short";
    }

    const std::span<const uint8_t> data = zip.subspan(static_cast<std::size_t>(data_offset), central.compressed_size);
    entry.data.resize(central.size);
    if (central.method == kStored)
    {
        if (central.compressed_size != central.size)
        {
            return entry.name + ": the archive is damaged";
        }

        std::copy(data.begin(), data.end(), entry.data.begin());
    }
    else if (central.method == kDeflated)
    {
        if (auto error = Inflate(data, entry.data))
        {
            return entry.name + ": " + *error;
        }
    }
    else
    {
        return entry.name + " uses a compression method that " + std::string(program) + " doesn't read";
    }

    if (crc32(0, entry.data.data(), static_cast<uInt>(entry.data.size())) != central.crc)
    {
        return entry.name + ": the CRC-32 doesn't match";
    }

    return std::nullopt;
}

} // namespace

bool LooksLikeZip(std::span<const uint8_t> bytes)
{
    return bytes.size() >= 4 &&
           (LoadLe32(bytes.data()) == kLocalHeader || LoadLe32(bytes.data()) == kEndOfCentralDirectory);
}

std::optional<std::string> ReadZip(std::span<const uint8_t> zip, std::vector<ZipEntry>& entries,
                                   std::string_view program)
{
    Directory directory;
    if (std::optional<std::string> error = FindDirectory(zip, directory))
    {
        return error;
    }

    std::size_t position = directory.offset;
    for (uint16_t i = 0; i < directory.count; i++)
    {
        if (position + kCentralHeaderSize > directory.end || LoadLe32(zip.data() + position) != kCentralHeader)
        {
            return "the archive's directory is damaged";
        }

        const uint8_t* header = zip.data() + position;
        CentralEntry central;
        central.flags = LoadLe16(header + 8);
        central.method = LoadLe16(header + 10);
        central.crc = LoadLe32(header + 16);
        central.compressed_size = LoadLe32(header + 20);
        central.size = LoadLe32(header + 24);
        central.local_offset = LoadLe32(header + 42);
        const uint16_t name_size = LoadLe16(header + 28);
        const uint16_t extra_size = LoadLe16(header + 30);
        const uint16_t comment_size = LoadLe16(header + 32);
        if (position + kCentralHeaderSize + name_size > directory.end)
        {
            return "the archive's directory is damaged";
        }

        ZipEntry entry;
        entry.name.assign(reinterpret_cast<const char*>(header + kCentralHeaderSize), name_size);
        position += kCentralHeaderSize + name_size + extra_size + comment_size;
        if (!entry.name.empty() && entry.name.back() == '/')
        {
            continue;
        }

        if (std::optional<std::string> error = ReadEntryData(zip, central, program, entry))
        {
            return error;
        }

        entries.push_back(std::move(entry));
    }

    return std::nullopt;
}

} // namespace xsflib::rip
