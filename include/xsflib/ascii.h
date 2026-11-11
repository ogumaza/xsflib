// SPDX-License-Identifier: MIT

// ASCII lowercasing for tag names, file names and extensions. Unlike std::tolower, it doesn't depend on the C locale. A
// host program such as foobar2000 may have changed that locale.

#pragma once

#include <string>

namespace xsflib
{

// An ASCII letter in lower case, and any other character as it is.
inline char AsciiLower(char c)
{
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

inline std::string AsciiLower(std::string s)
{
    for (char& c : s)
    {
        c = AsciiLower(c);
    }

    return s;
}

} // namespace xsflib
