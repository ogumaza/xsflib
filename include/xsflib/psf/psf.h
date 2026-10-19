// SPDX-License-Identifier: MIT

// The PSF container that every xSF format uses: the header, the reserved area, the program compressed with zlib, and
// the tags. Each format defines its version byte, its reserved area and the chunks of its program.
//
// xsflib::psf_container holds everything here that requires no zlib. A reader with a decompressor of its own links only
// that. xsflib::psf adds zlib: InflateProgram, the ParsePsf that uses it, WritePsf, and streaming.h.

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "xsflib/text.h"

namespace xsflib::psf
{

// Times past these are treated as malformed. The limits keep the arithmetic far from overflowing. That applies here and
// where a player converts times into sample counts.
constexpr long long kMaxTimeField = 1'000'000'000; // hours or minutes
constexpr double kMaxTimeMs = 1e13;                // about 317 years

// Limits on a set: _lib nesting, files in a set, and the size of one file's program. They stop a malformed set from
// making the loader recurse, load or inflate without end.
constexpr int kMaxLibDepth = 10;
constexpr std::size_t kMaxSetFiles = 64;
constexpr std::size_t kMaxProgramSize = std::size_t{1} << 30;

// Tags from a PSF file's [TAG] section. Names are lower-cased. Multi-line values are joined with '\n'.
using Tags = std::map<std::string, std::string>;

// One parsed PSF file.
struct PsfFile
{
    uint8_t version = 0;
    std::vector<uint8_t> reserved;
    std::vector<uint8_t> program; // decompressed
    Tags tags;
    std::size_t compressed_size = 0; // the program's size in the file
};

// Checks and decompresses a program: its compressed bytes and the header's CRC-32 in, the program out. Returns the
// error, or nothing.
using Decompressor = std::function<std::optional<std::string>(const uint8_t* data, std::size_t size, uint32_t crc,
                                                              std::vector<uint8_t>& program)>;

// How ParsePsf reads a file.
struct ParseOptions
{
    // Whether the program is decompressed and checked. A file read without it still gives the version, the reserved
    // area and the tags.
    bool with_program = true;

    // The version that the file must have. It's checked after the signature and before the sizes. Without it, any
    // version passes. The caller then checks it.
    std::optional<uint8_t> version;

    // The errors for a file without the signature, with another version, or shorter than its header says.
    std::string not_psf = "not a PSF file";
    std::string wrong_version = "wrong PSF version";
    std::string truncated = "truncated PSF file";

    // Checks and decompresses the program. Its error comes back unchanged. A program with no decompressor fails.
    Decompressor decompress;
};

// Parses a PSF file. Returns the error, or nothing.
std::optional<std::string> ParsePsf(const std::vector<uint8_t>& data, PsfFile& out, const ParseOptions& options);

// Parses a PSF file with InflateProgram as the decompressor. Without `with_program`, the program isn't decompressed or
// checked. In xsflib::psf.
std::optional<std::string> ParsePsf(const std::vector<uint8_t>& data, PsfFile& out, bool with_program = true);

// zlib's decompressor: checks the program against the CRC-32, then inflates it to at most kMaxProgramSize bytes. In
// xsflib::psf.
std::optional<std::string> InflateProgram(const uint8_t* data, std::size_t size, uint32_t crc,
                                          std::vector<uint8_t>& program);

// Offset of the tag area (the end of the program) of a PSF file, or nullopt if the header is invalid.
std::optional<std::size_t> TagAreaOffset(const std::vector<uint8_t>& data);

// The tag area for `tags`: "[TAG]", then name=value lines. A multi-line value becomes one line for each of its lines,
// all with the same name. Empty when there are no tags.
std::string FormatTagArea(const Tags& tags);

// Parses the text of a tag area after "[TAG]": name=value lines. Lines without '=' or without a name are skipped. A
// name on consecutive lines makes a multi-line value.
Tags ParseTags(const char* text, std::size_t size);

// Writes a PSF file. The program is compressed by zlib at its highest level. Throws std::runtime_error if zlib fails.
// Only running out of memory can make zlib fail. In xsflib::psf.
std::vector<uint8_t> WritePsf(uint8_t version, const std::vector<uint8_t>& reserved,
                              const std::vector<uint8_t>& program, const Tags& tags);

// Numbers read from the start of a tag's value (xsflib's text.h).
using xsflib::DecimalNumber;
using xsflib::ReadDecimal;

// Parses a PSF length or fade tag ("seconds", "m:s" or "h:m:s", with '.' or ',' before decimals) into milliseconds.
// Returns -1 if the tag is empty or malformed.
long long ParseTime(const std::string& text);

// Formats milliseconds as a PSF time ("m:ss.fff").
std::string FormatTime(long long ms);

// The time that the tag `name` gives, such as length or fade, in milliseconds. Returns -1 if the tag is missing, empty
// or malformed.
long long TagTimeMs(const Tags& tags, const std::string& name);

// The volume tag's factor, or 1.0 without one.
double TagVolume(const Tags& tags);

// Whether `name` is one of the PSF tags that aren't metadata: the libraries (_lib, _lib2 and on), length, fade, volume
// and utf8.
bool IsSystemTag(std::string_view name);

// The sequence variables that a format's variables tag (such as xyz_var) sets before a sequence starts, by number: 0 to
// 15 are the sound's player variables, and 16 to 31 the global ones.
using Variables = std::map<int, int16_t>;

// Parses a variables tag: one or more assignments "N=V", separated by commas or white space, line breaks included. N is
// 0 to 31 and V -32768 to 32767, each in decimal or in hex with a 0x prefix. Either may have a minus sign. A later
// assignment to the same variable wins. Returns nullopt if any part is malformed or out of range, or if there's no
// assignment.
std::optional<Variables> ParseVariables(std::string_view text);

// The variables tag for `variables`: the assignments in increasing order of N, in decimal, separated by ", ".
std::string FormatVariables(const Variables& variables);

// Reads a file by path. Returns false if the file can't be read.
using FileReader = std::function<bool(const std::string& path, std::vector<uint8_t>& data)>;

// Reads a whole file from disk.
bool ReadWholeFile(const std::string& path, std::vector<uint8_t>& data);

// The directory part of `path`, with its trailing separator: the path up to the last of `separators`. Paths only go to
// a FileReader. A FileReader may not use the file system (foobar2000 passes file:// URLs). Paths therefore stay
// strings.
std::string DirectoryOf(const std::string& path, std::string_view separators);

// Hex for messages (xsflib's text.h).
using xsflib::Hex;

// Appends `x` to `v` as a little-endian word.
void Put32(std::vector<uint8_t>& v, uint32_t x);

// `s` without the white space and control characters at its ends.
std::string Trim(const std::string& s);

} // namespace xsflib::psf
