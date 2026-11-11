// SPDX-License-Identifier: MIT

// UTF-8 in the names and tags that files hold: the decoding and encoding of one character, the check of a whole text,
// and a name's printable form. AppendUtf16 converts UTF-16 text to UTF-8.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
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

// How AppendUtf16 reads UTF-16 text, and what it changes.
struct Utf16Rules
{
    bool big_endian = false;           // the byte order of each 16-bit unit
    bool stop_at_nul = false;          // the text ends at a unit of 0
    bool lone_surrogate_bytes = false; // a lone surrogate becomes its three bytes in place of U+FFFD
    bool controls_as_spaces = false;   // a code point below U+0020 becomes a space
    bool trim_spaces = false;          // the spaces at either end of the converted text are dropped
};

// Appends the UTF-16 text `text` to `out` in UTF-8, as `rules` say. A high surrogate followed by a low one becomes one
// code point. A surrogate without its partner becomes U+FFFD. An odd byte at the end is ignored.
void AppendUtf16(std::string& out, std::span<const uint8_t> text, const Utf16Rules& rules = {});

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
