// SPDX-License-Identifier: MIT

// Unit tests for the encoding of a character in UTF-8 (AppendUtf8, xsflib/utf8.h): the examples of RFC 3629, the ends
// of each length, and the values that UTF-8 can't hold. Every code point that it encodes decodes back with
// Utf8Character.

#include "xsflib/utf8.h"

#include <cstdint>
#include <string>

#include "xsflib/testing/check.h"

namespace
{

std::string Encoded(uint32_t c)
{
    std::string out;
    xsflib::AppendUtf8(out, c);

    return out;
}

void TestExamples()
{
    // The characters of RFC 3629's examples: A, the not-identical-to sign, the Greek capital alpha, the full stop, a
    // Hangul syllable, the Japanese for "Japanese", and a character outside the BMP.
    XSFLIB_CHECK(Encoded(0x41) == "\x41");
    XSFLIB_CHECK(Encoded(0x2262) == "\xe2\x89\xa2");
    XSFLIB_CHECK(Encoded(0x391) == "\xce\x91");
    XSFLIB_CHECK(Encoded(0x2e) == "\x2e");
    XSFLIB_CHECK(Encoded(0xd55c) == "\xed\x95\x9c");
    XSFLIB_CHECK(Encoded(0x65e5) + Encoded(0x672c) + Encoded(0x8a9e) == "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e");
    XSFLIB_CHECK(Encoded(0x233b4) == "\xf0\xa3\x8e\xb4");

    // The first and last code point of each length.
    XSFLIB_CHECK(Encoded(0) == std::string(1, '\0'));
    XSFLIB_CHECK(Encoded(0x7f) == "\x7f");
    XSFLIB_CHECK(Encoded(0x80) == "\xc2\x80");
    XSFLIB_CHECK(Encoded(0x7ff) == "\xdf\xbf");
    XSFLIB_CHECK(Encoded(0x800) == "\xe0\xa0\x80");
    XSFLIB_CHECK(Encoded(0xffff) == "\xef\xbf\xbf");
    XSFLIB_CHECK(Encoded(0x10000) == "\xf0\x90\x80\x80");
    XSFLIB_CHECK(Encoded(0x10ffff) == "\xf4\x8f\xbf\xbf");

    // Surrogates and values past U+10FFFF become U+FFFD.
    const std::string kReplacement = "\xef\xbf\xbd";

    XSFLIB_CHECK(Encoded(0xd800) == kReplacement && Encoded(0xdbff) == kReplacement &&
                 Encoded(0xdc00) == kReplacement && Encoded(0xdfff) == kReplacement);
    XSFLIB_CHECK(Encoded(0x110000) == kReplacement && Encoded(0xffffffff) == kReplacement);
}

void TestAppends()
{
    std::string text = "a";

    xsflib::AppendUtf8(text, 0xe9);

    XSFLIB_CHECK(text == "a\xc3\xa9");
}

void TestRoundTrips()
{
    int wrong = 0;
    for (uint32_t c = 0; c <= 0x10ffff; c++)
    {
        if (c >= 0xd800 && c < 0xe000)
        {
            continue;
        }

        const std::string bytes = Encoded(c);
        const auto [decoded, length] = xsflib::Utf8Character(bytes);
        if (decoded != c || length != bytes.size())
        {
            wrong++;
        }
    }

    XSFLIB_CHECK(wrong == 0);
}

} // namespace

int main()
{
    TestExamples();
    TestAppends();
    TestRoundTrips();

    return xsflib::testing::Summary("utf8");
}
