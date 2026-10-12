// SPDX-License-Identifier: MIT

// UTF-8 in the names that files hold: the decoding of one character, and a name's printable form.

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

// Whether code point `c` is printable: not a control character, and not the line or paragraph separator (U+2028,
// U+2029).
bool IsPrintable(uint32_t c);

// Replaces control characters, line/paragraph separators and invalid UTF-8 bytes with '?'. These can disrupt terminal
// output and a macOS droplet app that reads it (xsflib_add_droplet): AppleScript splits on U+2029 and falls back to
// another encoding if any byte is invalid UTF-8. macOS also requires valid UTF-8 file names.
std::string PrintableName(std::string_view name);

} // namespace xsflib
