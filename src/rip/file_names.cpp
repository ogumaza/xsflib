// SPDX-License-Identifier: MIT

// The names of a rip's files (see file_names.h).

#include "xsflib/rip/file_names.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ios>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "xsflib/ascii.h"
#include "xsflib/files.h"
#include "xsflib/utf8.h"

namespace xsflib::rip
{
namespace
{

// ASCII letters, digits, '_', '-' and '.' are kept. So is each printable character outside ASCII (IsPrintable). Every
// other character becomes one '_': spaces, the rest of ASCII, control characters and the line and paragraph separators.
// So does each byte that isn't part of a valid UTF-8 character. Unlike std::isalnum, this doesn't depend on the C
// locale.
std::string SafeName(const std::string& s)
{
    std::string out;
    for (std::size_t i = 0; i < s.size();)
    {
        const char c = s[i];
        const auto [code, length] = Utf8Character(std::string_view(s).substr(i));
        if (length == 1)
        {
            const bool keep = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
                              c == '-' || c == '.';
            out += keep ? c : '_';
        }
        else
        {
            out += length != 0 && IsPrintable(code) ? s.substr(i, length) : "_";
        }

        i += std::max<std::size_t>(length, 1);
    }

    return out;
}

// Windows reserves these device names, whatever follows the first dot of a file name. COM and LPT take a digit or a
// superscript 1, 2 or 3.
bool ReservedOnWindows(const std::string& stem)
{
    const std::string name = AsciiLower(stem);
    const std::string number = name.starts_with("com") || name.starts_with("lpt") ? name.substr(3) : "";
    const bool numbered = (number.size() == 1 && number[0] >= '0' && number[0] <= '9') || number == "\xc2\xb9" ||
                          number == "\xc2\xb2" || number == "\xc2\xb3";
    return name == "con" || name == "prn" || name == "aux" || name == "nul" || numbered;
}

// True if the file at `path` holds exactly `data`. The sizes are compared first. The file is read only when they agree.
// The comparison reads blocks of 1 MiB. A library can be hundreds of megabytes. A folder at `path`, or a file that
// can't be read, doesn't hold `data`.
bool FileHolds(const std::filesystem::path& path, std::span<const uint8_t> data)
{
    std::error_code ec;
    if (std::filesystem::file_size(path, ec) != data.size() || ec)
    {
        return false;
    }

    std::ifstream file(path, std::ios::binary);
    std::vector<char> block(std::size_t{1} << 20);
    for (std::size_t at = 0; at < data.size(); at += block.size())
    {
        const std::size_t size = std::min(block.size(), data.size() - at);
        if (!file.read(block.data(), static_cast<std::streamsize>(size)) ||
            std::memcmp(block.data(), data.data() + at, size) != 0)
        {
            return false;
        }
    }

    return true;
}

} // namespace

FileNames::FileNames(std::string library_extension, std::string mini_extension)
    : library_extension_(std::move(library_extension)), mini_extension_(std::move(mini_extension))
{
}

std::string FileNames::Library(const std::string& base)
{
    std::string name = SafeName(base);
    const std::size_t stem_length = std::min(name.find('.'), name.size());
    if (ReservedOnWindows(name.substr(0, stem_length)))
    {
        name.insert(stem_length, "_");
    }

    return Unique(name, library_extension_);
}

std::string FileNames::LibraryIn(const std::filesystem::path& folder, const std::string& base,
                                 std::span<const uint8_t> library)
{
    // exists() returns false when the file's status can't be read, such as in a folder without search permission. The
    // name is then used. Writing the library there fails with the rip's message.
    std::string name = Library(base);
    std::error_code ec;
    while (std::filesystem::exists(folder / PathFromUtf8(name), ec) && !FileHolds(folder / PathFromUtf8(name), library))
    {
        name = Library(base);
    }

    return name;
}

std::string FileNames::Mini(uint32_t index, const std::string& label)
{
    char prefix[16];
    std::snprintf(prefix, sizeof(prefix), "%04u ", static_cast<unsigned>(index));

    return Unique(prefix + SafeName(label), mini_extension_);
}

std::string FileNames::Unique(const std::string& stem, const std::string& extension)
{
    std::string name = stem + extension;
    for (int n = 2; !taken_.insert(AsciiLower(name)).second; n++)
    {
        name = stem + "_" + std::to_string(n) + extension;
    }

    return name;
}

} // namespace xsflib::rip
