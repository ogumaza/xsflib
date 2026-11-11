// SPDX-License-Identifier: MIT

// The PSF container, without zlib (see psf.h).

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <ios>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "header.h"
#include "xsflib/ascii.h"
#include "xsflib/bytes.h"
#include "xsflib/files.h"
#include "xsflib/psf/psf.h"
#include "xsflib/text.h"

namespace xsflib::psf
{
namespace
{

// The marker at the start of the tag area.
constexpr std::string_view kTagMarker = "[TAG]";

} // namespace

void Put32(std::vector<uint8_t>& v, uint32_t x)
{
    AppendLe32(v, x);
}

std::string Trim(const std::string& s)
{
    std::size_t a = 0, b = s.size();
    while (a < b && static_cast<unsigned char>(s[a]) <= 0x20)
    {
        a++;
    }

    while (b > a && static_cast<unsigned char>(s[b - 1]) <= 0x20)
    {
        b--;
    }

    return s.substr(a, b - a);
}

Tags ParseTags(const char* text, std::size_t size)
{
    Tags tags;
    std::string last;
    std::size_t pos = 0;
    while (pos < size)
    {
        std::size_t end = pos;
        while (end < size && text[end] != '\n')
        {
            end++;
        }

        std::string line(text + pos, end - pos);
        pos = end + 1;
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos)
        {
            continue;
        }

        const std::string name = AsciiLower(Trim(line.substr(0, eq)));
        const std::string value = Trim(line.substr(eq + 1));
        if (name.empty())
        {
            continue;
        }

        auto it = tags.find(name);
        if (it != tags.end() && name == last)
        {
            it->second += "\n" + value; // multi-line value
        }
        else
        {
            tags[name] = value;
        }

        last = name;
    }

    return tags;
}

std::string DirectoryOf(const std::string& path, std::string_view separators)
{
    const std::size_t separator = path.find_last_of(separators);
    return separator == std::string::npos ? std::string() : path.substr(0, separator + 1);
}

std::optional<std::size_t> TagAreaOffset(const std::vector<uint8_t>& data)
{
    const std::optional<detail::PsfHeader> header = detail::ReadPsfHeader(data);
    if (!header || !header->complete)
    {
        return std::nullopt;
    }

    return static_cast<std::size_t>(uint64_t{detail::kHeaderSize} + header->reserved_size + header->program_size);
}

std::optional<std::string> ParsePsf(const std::vector<uint8_t>& data, PsfFile& out, const ParseOptions& options)
{
    std::optional<detail::PsfHeader> header;
    const std::optional<std::string> header_error = detail::CheckPsfHeader(data, options, header);
    if (header)
    {
        out.version = header->version;
    }

    if (header_error)
    {
        return header_error;
    }

    const uint32_t reserved_size = header->reserved_size;
    const uint32_t program_size = header->program_size;
    const uint32_t crc = header->crc;
    const auto reserved = data.begin() + detail::kHeaderSize;
    out.reserved.assign(reserved, reserved + reserved_size);
    out.program.clear();
    out.tags.clear();
    out.compressed_size = program_size;

    if (program_size && options.with_program)
    {
        if (!options.decompress)
        {
            return "no decompressor for the program";
        }

        if (std::optional<std::string> error =
                options.decompress(data.data() + detail::kHeaderSize + reserved_size, program_size, crc, out.program))
        {
            return error;
        }
    }

    const std::size_t tag_start = detail::kHeaderSize + reserved_size + program_size;
    if (data.size() >= tag_start + kTagMarker.size() &&
        std::memcmp(data.data() + tag_start, kTagMarker.data(), kTagMarker.size()) == 0)
    {
        const std::size_t text_start = tag_start + kTagMarker.size();
        out.tags = ParseTags(reinterpret_cast<const char*>(data.data() + text_start), data.size() - text_start);
    }

    return std::nullopt;
}

std::string FormatTagArea(const Tags& tags)
{
    if (tags.empty())
    {
        return {};
    }

    std::string text(kTagMarker);
    for (const auto& [k, v] : tags)
    {
        const auto add_line = [&text, &name = k](std::string_view line)
        {
            text += name + "=" + std::string(line) + "\n";
        };

        ForEachTagLine(v, add_line);
    }

    return text;
}

long long ParseTime(const std::string& text)
{
    const std::string time = Trim(text);
    if (time.empty())
    {
        return -1;
    }

    std::vector<std::string> parts;
    std::size_t start = 0;
    while (true)
    {
        const std::size_t colon = time.find(':', start);
        parts.push_back(time.substr(start, colon == std::string::npos ? std::string::npos : colon - start));
        if (colon == std::string::npos)
        {
            break;
        }

        start = colon + 1;
    }

    if (parts.size() > 3)
    {
        return -1;
    }

    long long whole = 0; // hours and minutes, in minutes
    for (std::size_t i = 0; i + 1 < parts.size(); i++)
    {
        // An empty field counts as 0. Real tags hold times such as ":05".
        char* end = nullptr;
        const long long v = std::strtoll(parts[i].c_str(), &end, 10);
        if (*end || v < 0 || v > kMaxTimeField)
        {
            return -1;
        }

        whole = whole * 60 + v;
    }

    const std::string seconds_text = Trim(parts.back());
    const std::optional<DecimalNumber> seconds = ReadDecimal(seconds_text);
    if (!seconds || seconds->length != seconds_text.size())
    {
        return -1;
    }

    // The comparisons are written so that infinity fails them.
    const double ms = (static_cast<double>(whole) * 60.0 + seconds->value) * 1000.0 + 0.5;
    if (!(seconds->value >= 0) || !(ms <= kMaxTimeMs))
    {
        return -1;
    }

    return static_cast<long long>(ms);
}

std::string FormatTime(long long ms)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%lld:%02lld.%03lld", ms / 60000, (ms / 1000) % 60, ms % 1000);

    return buf;
}

long long TagTimeMs(const Tags& tags, const std::string& name)
{
    const auto it = tags.find(name);
    return it == tags.end() ? -1 : ParseTime(it->second);
}

double TagVolume(const Tags& tags)
{
    const auto it = tags.find("volume");
    if (it == tags.end())
    {
        return 1.0;
    }

    // Text after the number, as in "0.5dB", is ignored. So is a value that isn't a finite number, such as 1e999: it
    // would turn every sample into nonsense.
    const std::optional<DecimalNumber> volume = ReadDecimal(it->second);
    return volume && std::isfinite(volume->value) ? volume->value : 1.0;
}

bool IsSystemTag(std::string_view name)
{
    return name.starts_with("_lib") || name == "length" || name == "fade" || name == "volume" || name == "utf8";
}

bool ReadWholeFile(const std::string& path, std::vector<uint8_t>& data)
{
    // The path is narrow text. A FileReader may take paths that aren't the file system's.
    std::ifstream f(path, std::ios::binary);
    if (!f)
    {
        return false;
    }

    return ReadStream(f, data);
}

std::optional<Variables> ParseVariables(std::string_view text, int max_variable)
{
    Variables variables;
    for (const std::string_view item : SplitList(text))
    {
        const std::optional<Assignment> assignment = ParseAssignment(item);
        if (!assignment || assignment->index < 0 || assignment->index > max_variable || assignment->value < INT16_MIN ||
            assignment->value > INT16_MAX)
        {
            return std::nullopt;
        }

        variables[static_cast<int>(assignment->index)] = static_cast<int16_t>(assignment->value);
    }

    if (variables.empty())
    {
        return std::nullopt;
    }

    return variables;
}

std::string FormatVariables(const Variables& variables)
{
    std::string text;
    for (const auto& [index, value] : variables)
    {
        text += (text.empty() ? "" : ", ") + std::to_string(index) + "=" + std::to_string(value);
    }

    return text;
}

} // namespace xsflib::psf
