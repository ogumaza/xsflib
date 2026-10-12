// SPDX-License-Identifier: MIT

// Checks the common files' helpers for files and numbers (xsflib/files.h and xsflib/text.h): UTF-8 paths, reading and
// writing whole files, hex, and whole numbers from text.

#include "xsflib/files.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include "xsflib/testing/check.h"
#include "xsflib/text.h"

using xsflib::testing::failures;

namespace fs = std::filesystem;

namespace
{

void TestPaths()
{
    // A name outside every ANSI code page comes back as it went in.
    const std::string kName = "Ünïcode \xe5\x90\x8d\xe5\x89\x8d/set.minixyz";

    XSFLIB_CHECK(xsflib::Utf8(xsflib::PathFromUtf8(kName)) == kName);
    XSFLIB_CHECK(xsflib::Utf8(xsflib::PathFromUtf8(kName).filename()) == "set.minixyz");
}

void TestFiles()
{
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path folder = fs::temp_directory_path() / ("xsflib_files_test_" + std::to_string(stamp));
    fs::create_directories(folder);
    const fs::path path = folder / xsflib::PathFromUtf8("d\xc3\xa9j\xc3\xa0 vu.bin");

    // A file written whole reads back whole.
    std::vector<uint8_t> data(70000);
    for (std::size_t i = 0; i < data.size(); i++)
    {
        data[i] = static_cast<uint8_t>(i * 13 + (i >> 9));
    }

    XSFLIB_CHECK(!xsflib::WriteFile(path, data));
    std::vector<uint8_t> read;
    XSFLIB_CHECK(xsflib::ReadFile(path, read) && read == data);

    // A size limit rejects a larger file. An empty file reads as empty.
    XSFLIB_CHECK(!xsflib::ReadFile(path, read, data.size() - 1));
    XSFLIB_CHECK(xsflib::ReadFile(path, read, data.size()) && read == data);
    XSFLIB_CHECK(!xsflib::WriteFile(path, std::vector<uint8_t>{}));
    XSFLIB_CHECK(xsflib::ReadFile(path, read) && read.empty());

    // A file that isn't there can't be read, or written in a folder that isn't there.
    const fs::path missing = folder / "no folder" / "file.bin";

    XSFLIB_CHECK(!xsflib::ReadFile(missing, read));
    XSFLIB_CHECK(xsflib::WriteFile(missing, data) == "can't write " + xsflib::Utf8(missing));

    // A stream reads whole too.
    std::istringstream stream("abc\0def", std::ios::binary);
    XSFLIB_CHECK(xsflib::ReadStream(stream, read) && read == std::vector<uint8_t>({'a', 'b', 'c'}));

    std::error_code ec;
    fs::remove_all(folder, ec);
}

void TestNumbers()
{
    XSFLIB_CHECK(xsflib::Hex(0) == "0x0");
    XSFLIB_CHECK(xsflib::Hex(0x1f) == "0x1f");
    XSFLIB_CHECK(xsflib::Hex(0x1f, 8) == "0x0000001f");
    XSFLIB_CHECK(xsflib::Hex(0xffffffff, 8) == "0xffffffff");
    XSFLIB_CHECK(xsflib::Hex(0xabc, 8, true) == "0x00000ABC");

    XSFLIB_CHECK(xsflib::ParseInteger("12") == 12);
    XSFLIB_CHECK(xsflib::ParseInteger("-12") == -12);
    XSFLIB_CHECK(xsflib::ParseInteger("0x1F") == 31);
    XSFLIB_CHECK(xsflib::ParseInteger("0X1f") == 31);
    XSFLIB_CHECK(xsflib::ParseInteger("-0x10") == -16);
    XSFLIB_CHECK(xsflib::ParseInteger("4294967295") == 4294967295);
    XSFLIB_CHECK(!xsflib::ParseInteger("4294967296"));
    XSFLIB_CHECK(!xsflib::ParseInteger(""));
    XSFLIB_CHECK(!xsflib::ParseInteger("-"));
    XSFLIB_CHECK(!xsflib::ParseInteger("0x"));
    XSFLIB_CHECK(!xsflib::ParseInteger(" 1"));
    XSFLIB_CHECK(!xsflib::ParseInteger("1 "));
    XSFLIB_CHECK(!xsflib::ParseInteger("1.5"));

    // The reason for no number, and a rule without the minus sign.
    xsflib::IntegerError why = xsflib::IntegerError::kOutOfRange;

    XSFLIB_CHECK(!xsflib::ParseInteger("12a", {}, &why) && why == xsflib::IntegerError::kNotNumber);
    XSFLIB_CHECK(!xsflib::ParseInteger("0x100000000", {}, &why) && why == xsflib::IntegerError::kOutOfRange);
    XSFLIB_CHECK(!xsflib::ParseInteger("99999999999a", {}, &why) && why == xsflib::IntegerError::kNotNumber);
    XSFLIB_CHECK(!xsflib::ParseInteger("-5", {.minus = false}, &why) && why == xsflib::IntegerError::kNotNumber);

    // Base 16 takes the prefix or not. Base 10 takes none.
    const xsflib::IntegerRules kHex = {16};

    XSFLIB_CHECK(xsflib::ParseInteger("8000000", kHex) == 0x8000000 &&
                 xsflib::ParseInteger("0x8000000", kHex) == 0x8000000);
    XSFLIB_CHECK(!xsflib::ParseInteger("0x10", {10}) && xsflib::ParseInteger("10", {10}) == 10);

    // C's rules take white space, a plus sign and 64 bits, and hold larger numbers to 64 bits.
    const xsflib::IntegerRules kC10 = {10, true};
    const xsflib::IntegerRules kC16 = {16, true};

    XSFLIB_CHECK(xsflib::ParseInteger(" 5", kC10) == 5 && xsflib::ParseInteger("+5", kC10) == 5);
    XSFLIB_CHECK(xsflib::ParseInteger("5000000000", kC10) == 5000000000);
    XSFLIB_CHECK(xsflib::ParseInteger("99999999999999999999", kC10) == INT64_MAX);
    XSFLIB_CHECK(xsflib::ParseInteger("8000123", kC16) == 0x8000123 &&
                 xsflib::ParseInteger("0X8000123", kC16) == 0x8000123);
    XSFLIB_CHECK(!xsflib::ParseInteger("", kC10) && !xsflib::ParseInteger(" ", kC10) &&
                 !xsflib::ParseInteger("5 ", kC10));
    XSFLIB_CHECK(!xsflib::ParseInteger("0x", kC16) && !xsflib::ParseInteger("12g", kC16));

    // An assignment "N=V" takes ParseInteger's numbers on either side of the first '='.
    const auto assignment = xsflib::ParseAssignment("3=-0x10");
    XSFLIB_CHECK(assignment && assignment->index == 3 && assignment->value == -16);

    XSFLIB_CHECK(!xsflib::ParseAssignment("3") && !xsflib::ParseAssignment("=5") && !xsflib::ParseAssignment("3="));
    XSFLIB_CHECK(!xsflib::ParseAssignment("3=4=5") && !xsflib::ParseAssignment(" 3=4") &&
                 !xsflib::ParseAssignment("a=4"));
    XSFLIB_CHECK(!xsflib::ParseAssignment("1=-1", {.minus = false}) && xsflib::ParseAssignment("1=-1"));

    // Decimals: a comma counts as a point without C's rules. C's rules take white space, hex and infinity.
    const auto comma = xsflib::ReadDecimal("8000,5");
    XSFLIB_CHECK(comma && comma->value == 8000.5 && comma->length == 6);

    const xsflib::DecimalRules kC = {true};

    const auto c_comma = xsflib::ReadDecimal("8000,5", kC);
    XSFLIB_CHECK(c_comma && c_comma->value == 8000.0 && c_comma->length == 4);

    const auto spaced = xsflib::ReadDecimal(" 8000", kC);
    XSFLIB_CHECK(spaced && spaced->value == 8000.0 && spaced->length == 5 && !xsflib::ReadDecimal(" 8000"));

    const auto hex_float = xsflib::ReadDecimal("0x1F40", kC);
    XSFLIB_CHECK(hex_float && hex_float->value == 8000.0 && hex_float->length == 6);

    XSFLIB_CHECK(!xsflib::ReadDecimal("", kC) && !xsflib::ReadDecimal("x", kC) && !xsflib::ReadDecimal(" ", kC));
}

} // namespace

int main()
{
    TestPaths();
    TestFiles();
    TestNumbers();

    if (failures)
    {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }

    std::printf("files tests passed\n");

    return 0;
}
