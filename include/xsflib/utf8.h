// SPDX-License-Identifier: MIT

// UTF-8 in the names and tags that files hold: the decoding and encoding of one character, the check of a whole text,
// and a name's printable form.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace xsflib
{

// Decodes the first UTF-8 character in nonempty `s`. Returns its code point and byte length, or length 0 for invalid
// UTF-8.
std::pair<uint32_t, std::size_t> Utf8Character(std::string_view s);

// Appends code point `c` to `out` in UTF-8. UTF-8 can't hold a surrogate or a value above 0x10FFFF. AppendUtf8 writes
// U+FFFD in its place.
inline void AppendUtf8(std::string& out, uint32_t c)
{
    if ((c >= 0xd800 && c < 0xe000) || c > 0x10ffff)
    {
        c = 0xfffd;
    }

    if (c < 0x80)
    {
        out += static_cast<char>(c);
    }
    else if (c < 0x800)
    {
        out += static_cast<char>(0xc0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3f));
    }
    else if (c < 0x10000)
    {
        out += static_cast<char>(0xe0 | (c >> 12));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3f));
        out += static_cast<char>(0x80 | (c & 0x3f));
    }
    else
    {
        out += static_cast<char>(0xf0 | (c >> 18));
        out += static_cast<char>(0x80 | ((c >> 12) & 0x3f));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3f));
        out += static_cast<char>(0x80 | (c & 0x3f));
    }
}

// Whether `text` is valid UTF-8 throughout.
bool IsUtf8(std::string_view text);

// Whether code point `c` is printable: not a control character, and not the line or paragraph separator (U+2028,
// U+2029).
bool IsPrintable(uint32_t c);

// Replaces control characters, line/paragraph separators and invalid UTF-8 bytes with '?'. These can disrupt terminal
// output and a macOS droplet app that reads it (xsflib_add_droplet): AppleScript splits on U+2029 and falls back to
// another encoding if any byte is invalid UTF-8. macOS also requires valid UTF-8 file names.
std::string PrintableName(std::string_view name);

} // namespace xsflib
