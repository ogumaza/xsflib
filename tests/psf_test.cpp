// SPDX-License-Identifier: MIT

// Unit tests for the PSF container (xsflib/psf/psf.h): times, decimal numbers, writing and parsing files, the parsing
// options, tags and the variables tag.

#include "xsflib/psf/psf.h"

#include <algorithm>
#include <clocale>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <zlib.h>

#include "xsflib/bytes.h"
#include "xsflib/testing/check.h"

using namespace xsflib::psf;
using xsflib::LoadLe32;
using xsflib::StoreLe32;
using xsflib::testing::Check;

namespace
{

// A version byte for the files that the tests write. Any value works.
constexpr uint8_t kVersion = 0x27;

void TestTimes()
{
    XSFLIB_CHECK(ParseTime("") == -1);
    XSFLIB_CHECK(ParseTime("abc") == -1);
    XSFLIB_CHECK(ParseTime("5") == 5000);
    XSFLIB_CHECK(ParseTime("1.5") == 1500);
    XSFLIB_CHECK(ParseTime("1,25") == 1250);
    XSFLIB_CHECK(ParseTime("2:03") == 123000);
    XSFLIB_CHECK(ParseTime("2:03.5") == 123500);
    XSFLIB_CHECK(ParseTime("1:00:00") == 3600000);
    XSFLIB_CHECK(ParseTime(" 0:10.000 ") == 10000);
    XSFLIB_CHECK(ParseTime("1:2:3:4") == -1);
    XSFLIB_CHECK(ParseTime("-1") == -1);
    XSFLIB_CHECK(ParseTime("nan") == -1);
    XSFLIB_CHECK(ParseTime("inf") == -1);
    XSFLIB_CHECK(ParseTime("1e300") == -1);
    XSFLIB_CHECK(ParseTime("99999999999999999999:00") == -1);

    XSFLIB_CHECK(FormatTime(0) == "0:00.000");
    XSFLIB_CHECK(FormatTime(65998) == "1:05.998");
    XSFLIB_CHECK(FormatTime(600000) == "10:00.000");

    for (long long ms : {0LL, 1LL, 999LL, 59999LL, 61001LL, 3599999LL})
    {
        XSFLIB_CHECK(ParseTime(FormatTime(ms)) == ms);
    }
}

// Numbers in tags: an optional sign, digits with '.' or ',' before decimals, and an optional exponent, with the values
// that std::strtod gives in the C locale. Text after the number is left unread.
void TestDecimals()
{
    for (const char* text : {"0", "0.5", "1,25", "-1.5", "+2", ".5", "5.", "123.456", "0.000123", "2.718281828459045",
                             "1e3", "1.5E-3", "1e22", "1e-22", "9007199254740992", "0.1", "0.3", "62.226"})
    {
        std::string c_text = text;
        std::replace(c_text.begin(), c_text.end(), ',', '.');

        const auto number = ReadDecimal(text);

        Check(number && number->length == std::strlen(text) && number->value == std::strtod(c_text.c_str(), nullptr),
              std::string(text) + " reads as std::strtod reads it");
    }

    const auto with_unit = ReadDecimal("0.5dB");
    const auto bare_exponent = ReadDecimal("1e+x");
    const auto hex = ReadDecimal("0x10");
    const auto huge = ReadDecimal("1e999");
    const auto tiny = ReadDecimal("1e-999");

    XSFLIB_CHECK(with_unit && with_unit->value == 0.5 && with_unit->length == 3);
    XSFLIB_CHECK(bare_exponent && bare_exponent->value == 1 && bare_exponent->length == 1);
    XSFLIB_CHECK(hex && hex->value == 0 && hex->length == 1);
    XSFLIB_CHECK(huge && std::isinf(huge->value));
    XSFLIB_CHECK(tiny && tiny->value == 0);

    for (const char* text : {"", ".", "-", "+.", "e5", " 1", "nan", "inf", "x1"})
    {
        Check(!ReadDecimal(text), std::string("\"") + text + "\" isn't a number");
    }
}

// Under a locale that writes decimals with a comma, std::strtod stops at '.'. Tags still read the same. The check runs
// only where such a locale is installed.
void TestDecimalsUnderCommaLocale()
{
    const char* comma_locale = nullptr;
    for (const char* name : {"de_DE.UTF-8", "de_DE.utf8", "de_DE", "fr_FR.UTF-8", "de-DE", "German_Germany.1252"})
    {
        if (std::setlocale(LC_NUMERIC, name) && std::strtod("0.5", nullptr) == 0)
        {
            comma_locale = name;
            break;
        }
    }

    if (!comma_locale)
    {
        std::setlocale(LC_NUMERIC, "C");
        std::printf("no locale with decimal commas is installed. Tags weren't read under one.\n");
        return;
    }

    const auto volume = ReadDecimal("0.5");
    const long long length = ParseTime("1:02.226");
    std::setlocale(LC_NUMERIC, "C");

    Check(volume && volume->value == 0.5, std::string("a volume of 0.5 reads as 0.5 under ") + comma_locale);
    Check(length == 62226, std::string("a length of 1:02.226 reads as 62,226 ms under ") + comma_locale);
}

void TestPsfRoundTrip()
{
    std::vector<uint8_t> reserved = {1, 2, 3, 4, 5};
    Tags tags = {{"title", "Test"}, {"comment", "line one\nline two"}, {"_lib", "x.xyzlib"}};
    std::vector<uint8_t> program(100000);
    for (std::size_t i = 0; i < program.size(); i++)
    {
        program[i] = static_cast<uint8_t>(i * 7 + (i >> 8));
    }

    const auto file = WritePsf(kVersion, reserved, program, tags);
    PsfFile psf;
    const auto err = ParsePsf(file, psf);

    XSFLIB_CHECK(file.size() > 16 && std::memcmp(file.data(), "PSF", 3) == 0 && file[3] == 0x27);
    XSFLIB_CHECK(!err);
    XSFLIB_CHECK(psf.version == kVersion);
    XSFLIB_CHECK(psf.reserved == reserved);
    XSFLIB_CHECK(psf.program == program);
    XSFLIB_CHECK(psf.tags == tags);
    XSFLIB_CHECK(TagAreaOffset(file) == 16 + reserved.size() + LoadLe32(&file[8]));

    // Corruption of the compressed program is detected by the CRC.
    auto bad = file;
    bad[16 + reserved.size() + 10] ^= 0x55;

    XSFLIB_CHECK(ParsePsf(bad, psf) == "program CRC mismatch");

    // Truncation.
    auto cut = file;
    cut.resize(20);

    XSFLIB_CHECK(ParsePsf(cut, psf) == "truncated PSF file");
    XSFLIB_CHECK(ParsePsf(std::vector<uint8_t>{'P', 'S'}, psf) == "not a PSF file");

    // A program whose zlib stream stops short, with a size and CRC that match what's left of it.
    const uint32_t half = LoadLe32(&file[8]) / 2;
    std::vector<uint8_t> short_stream(file.begin(), file.begin() + 16 + reserved.size() + half);
    StoreLe32(&short_stream[8], half);
    StoreLe32(&short_stream[12], static_cast<uint32_t>(crc32(0, &short_stream[16 + reserved.size()], half)));

    XSFLIB_CHECK(ParsePsf(short_stream, psf) == "truncated program data");
}

void TestTagParsing()
{
    const std::string kHeader = std::string("PSF") + static_cast<char>(kVersion) + std::string(12, '\0');
    const std::string kTaggedPsf = kHeader +
                                   "[TAG]Title = Hello \nTITLE2=x\ncomment=a\ncomment=b\nartist=me\ncomment=c\n"
                                   "no equals sign\n=empty name\nlength=1:00\r\n";
    std::vector<uint8_t> data(kTaggedPsf.begin(), kTaggedPsf.end());
    PsfFile psf;

    XSFLIB_CHECK(!ParsePsf(data, psf));
    XSFLIB_CHECK(psf.tags["title"] == "Hello");
    XSFLIB_CHECK(psf.tags["title2"] == "x");
    XSFLIB_CHECK(psf.tags["comment"] == "c"); // a and b join. The later c replaces them.
    XSFLIB_CHECK(psf.tags["artist"] == "me");
    XSFLIB_CHECK(psf.tags["length"] == "1:00");
    XSFLIB_CHECK(psf.tags.count("") == 0);

    // Consecutive lines with the same name join.
    const std::string kRepeatedTagPsf = kHeader + "[TAG]comment=a\ncomment=b\n";
    std::vector<uint8_t> data2(kRepeatedTagPsf.begin(), kRepeatedTagPsf.end());

    XSFLIB_CHECK(!ParsePsf(data2, psf));
    XSFLIB_CHECK(psf.tags["comment"] == "a\nb");
}

void TestParseOptions()
{
    const std::vector<uint8_t> kProgram = {1, 2, 3, 4, 5, 6, 7, 8};
    const auto file = WritePsf(kVersion, {9}, kProgram, {{"_lib", "a.lib"}});
    const uint32_t compressed_size = LoadLe32(&file[8]);
    PsfFile psf;

    // Without a decompressor, a file whose program is read fails. Without the program, it parses.
    ParseOptions options;

    XSFLIB_CHECK(ParsePsf(file, psf, options) == "no decompressor for the program");

    options.with_program = false;

    XSFLIB_CHECK(!ParsePsf(file, psf, options));
    XSFLIB_CHECK(psf.program.empty() && psf.compressed_size == compressed_size && psf.tags["_lib"] == "a.lib");

    // The decompressor gets the compressed bytes and the header's CRC-32. ParsePsf returns the decompressor's program
    // and error unchanged.
    options.with_program = true;
    std::size_t seen_size = 0;
    uint32_t seen_crc = 0;
    options.decompress = [&](const uint8_t*, std::size_t size, uint32_t crc,
                             std::vector<uint8_t>& out) -> std::optional<std::string>
    {
        seen_size = size;
        seen_crc = crc;
        out = {42};

        return std::nullopt;
    };

    XSFLIB_CHECK(!ParsePsf(file, psf, options));
    XSFLIB_CHECK(seen_size == compressed_size && seen_crc == LoadLe32(&file[12]));
    XSFLIB_CHECK(psf.program == std::vector<uint8_t>{42});

    options.decompress = [](const uint8_t*, std::size_t, uint32_t, std::vector<uint8_t>&) -> std::optional<std::string>
    {
        return "broken stream";
    };

    XSFLIB_CHECK(ParsePsf(file, psf, options) == "broken stream");

    // InflateProgram is zlib's decompressor. It checks the CRC-32. A decompressor that skips the check reads the
    // program anyway.
    auto bad_crc = file;
    bad_crc[12] ^= 1;
    options.decompress = InflateProgram;

    XSFLIB_CHECK(ParsePsf(bad_crc, psf, options) == "program CRC mismatch");

    options.decompress = [](const uint8_t* data, std::size_t size, uint32_t, std::vector<uint8_t>& out)
    {
        return InflateProgram(data, size, static_cast<uint32_t>(crc32(0, data, static_cast<uInt>(size))), out);
    };

    XSFLIB_CHECK(!ParsePsf(bad_crc, psf, options) && psf.program == kProgram);

    // The expected version is checked before the sizes. Each error of the header has the caller's words.
    options.version = static_cast<uint8_t>(kVersion + 1);
    options.not_psf = "isn't a PSF file";
    options.wrong_version = "has another version";
    options.truncated = "is cut short";
    auto cut = file;
    cut.resize(20);

    XSFLIB_CHECK(ParsePsf(cut, psf, options) == "has another version");

    options.version = kVersion;

    XSFLIB_CHECK(ParsePsf(cut, psf, options) == "is cut short");
    XSFLIB_CHECK(ParsePsf(std::vector<uint8_t>{'P', 'S', 'F', kVersion}, psf, options) == "isn't a PSF file");
    XSFLIB_CHECK(!ParsePsf(file, psf, options) && psf.version == kVersion && psf.program == kProgram);
}

void TestVariables()
{
    XSFLIB_CHECK((ParseVariables("0=1") == Variables{{0, 1}}));
    XSFLIB_CHECK((ParseVariables("0=1, 17=-1") == Variables{{0, 1}, {17, -1}}));
    XSFLIB_CHECK((ParseVariables(" 17=-1\n0=1\r\n") == Variables{{0, 1}, {17, -1}}));
    XSFLIB_CHECK((ParseVariables("0x1f=0x7fff,31=-0x8000") == Variables{{31, -32768}}));
    XSFLIB_CHECK((ParseVariables("5=2 5=3") == Variables{{5, 3}}));
    XSFLIB_CHECK((ParseVariables("0=-32768\t31=32767") == Variables{{0, -32768}, {31, 32767}}));

    for (const char* bad : {"", " , ", "0", "=1", "0=", "0 = 1", "32=1", "-1=0", "0=32768", "0=-32769", "0=1x", "0=+1",
                            "0x=1", "a=1", "0=1;1=2", "0=4294967296"})
    {
        Check(!ParseVariables(bad), std::string("'") + bad + "' isn't read");
    }

    XSFLIB_CHECK(FormatVariables({{17, -1}, {0, 1}}) == "0=1, 17=-1");
    XSFLIB_CHECK(
        (ParseVariables(FormatVariables({{0, 1}, {16, -5}, {31, 300}})) == Variables{{0, 1}, {16, -5}, {31, 300}}));
}

} // namespace

int main()
{
    TestTimes();
    TestDecimals();
    TestDecimalsUnderCommaLocale();
    TestPsfRoundTrip();
    TestTagParsing();
    TestParseOptions();
    TestVariables();

    return xsflib::testing::Summary("psf");
}
