// SPDX-License-Identifier: MIT

// Unit tests for the conversion of UTF-16 text to UTF-8 (AppendUtf16, xsflib/utf8.h): the byte orders, surrogate pairs
// (RFC 2781), lone surrogates, an odd byte at the end, and each of the rules that change the text.

#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

#include "xsflib/testing/check.h"
#include "xsflib/utf8.h"

namespace
{

// The units in the byte order of `rules`, converted and appended to `out`.
std::string Converted(std::initializer_list<uint16_t> units, const xsflib::Utf16Rules& rules = {}, std::string out = "")
{
    std::vector<uint8_t> bytes;
    for (const uint16_t unit : units)
    {
        const auto high = static_cast<uint8_t>(unit >> 8);
        const auto low = static_cast<uint8_t>(unit);
        bytes.push_back(rules.big_endian ? high : low);
        bytes.push_back(rules.big_endian ? low : high);
    }

    xsflib::AppendUtf16(out, bytes, rules);

    return out;
}

const std::string kReplacement = "\xef\xbf\xbd";

void TestCharacters()
{
    // A, e with an acute accent, the Japanese for "Japanese", and U+1F600 as a surrogate pair.
    const std::string kText = "A\xc3\xa9\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e\xf0\x9f\x98\x80";

    XSFLIB_CHECK(Converted({0x41, 0xe9, 0x65e5, 0x672c, 0x8a9e, 0xd83d, 0xde00}) == kText);
    XSFLIB_CHECK(Converted({0x41, 0xe9, 0x65e5, 0x672c, 0x8a9e, 0xd83d, 0xde00}, {.big_endian = true}) == kText);

    // The first and last code points of each length, and the ends of the surrogate pairs.
    XSFLIB_CHECK(Converted({0x7f, 0x80, 0x7ff, 0x800, 0xffff}) == "\x7f\xc2\x80\xdf\xbf\xe0\xa0\x80\xef\xbf\xbf");
    XSFLIB_CHECK(Converted({0xd800, 0xdc00}) == "\xf0\x90\x80\x80");
    XSFLIB_CHECK(Converted({0xdbff, 0xdfff}) == "\xf4\x8f\xbf\xbf");

    // The units go after the text already in `out`.
    XSFLIB_CHECK(Converted({0x62}, {}, "a") == "ab");
}

void TestLoneSurrogates()
{
    // A high surrogate at the end, a high surrogate before another character, and a low surrogate alone.
    XSFLIB_CHECK(Converted({0x41, 0xd83d}) == "A" + kReplacement);
    XSFLIB_CHECK(Converted({0xd83d, 0x41}) == kReplacement + "A");
    XSFLIB_CHECK(Converted({0xde00, 0x41}) == kReplacement + "A");
    XSFLIB_CHECK(Converted({0xd83d, 0xd83d, 0xde00}) == kReplacement + "\xf0\x9f\x98\x80");

    // With lone_surrogate_bytes, each lone surrogate becomes its three bytes. A pair is still one code point.
    const xsflib::Utf16Rules kBytes = {.lone_surrogate_bytes = true};

    XSFLIB_CHECK(Converted({0xd800}, kBytes) == "\xed\xa0\x80");
    XSFLIB_CHECK(Converted({0xdfff, 0x41}, kBytes) ==
                 "\xed\xbf\xbf"
                 "A");
    XSFLIB_CHECK(Converted({0xd83d, 0xde00}, kBytes) == "\xf0\x9f\x98\x80");
}

void TestEnds()
{
    // An odd byte at the end is ignored.
    std::string out;
    const std::vector<uint8_t> odd = {0x41, 0x00, 0x42};
    xsflib::AppendUtf16(out, odd);

    XSFLIB_CHECK(out == "A");

    // A unit of 0 is a character, unless the text stops at it.
    XSFLIB_CHECK(Converted({0x41, 0, 0x42}) == std::string("A\0B", 3));
    XSFLIB_CHECK(Converted({0x41, 0, 0x42}, {.stop_at_nul = true}) == "A");
    XSFLIB_CHECK(Converted({0xd83d, 0, 0x42}, {.stop_at_nul = true}) == kReplacement);
    XSFLIB_CHECK(Converted({}).empty());
}

void TestSpaces()
{
    // Code points below U+0020 become spaces. U+0020 and U+007F stay as they are.
    const xsflib::Utf16Rules kSpaces = {.controls_as_spaces = true};

    XSFLIB_CHECK(Converted({0x09, 0x41, 0x1f, 0x20, 0x7f}, kSpaces) == " A  \x7f");
    XSFLIB_CHECK(Converted({0x09, 0x41}) == "\tA");

    // The spaces at either end of the converted text are dropped. The text already in `out` stays as it is.
    const xsflib::Utf16Rules kTrim = {.trim_spaces = true};

    XSFLIB_CHECK(Converted({0x20, 0x20, 0x41, 0x20, 0x42, 0x20}, kTrim) == "A B");
    XSFLIB_CHECK(Converted({0x20, 0x20}, kTrim).empty());
    XSFLIB_CHECK(Converted({0x20, 0x43, 0x20}, kTrim, " x ") == " x C");
    XSFLIB_CHECK(Converted({0x20, 0x20}, kTrim, "x ") == "x ");

    // A title in big-endian units that ends at a NUL, with a tab and spaces around it.
    const xsflib::Utf16Rules kTitle = {
        .big_endian = true, .stop_at_nul = true, .controls_as_spaces = true, .trim_spaces = true};

    XSFLIB_CHECK(Converted({0x20, 0x09, 0x53, 0x6f, 0x6e, 0x67, 0x20, 0, 0x58}, kTitle) == "Song");
}

} // namespace

int main()
{
    const auto tests = []
    {
        TestCharacters();
        TestLoneSurrogates();
        TestEnds();
        TestSpaces();
    };

    return xsflib::testing::RunTest("utf16", tests);
}
