// SPDX-License-Identifier: MIT

// UTF-8 in the names and tags that files hold (see utf8.h).

#include "xsflib/utf8.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace xsflib
{

std::pair<uint32_t, std::size_t> Utf8Character(std::string_view s)
{
    const auto byte = [&](std::size_t i)
    {
        return static_cast<uint8_t>(s[i]);
    };

    const uint8_t lead = byte(0);
    const std::size_t length = lead < 0x80   ? 1
                               : lead < 0xc2 ? 0
                               : lead < 0xe0 ? 2
                               : lead < 0xf0 ? 3
                               : lead < 0xf5 ? 4
                                             : 0;
    if (length == 0 || length > s.size())
    {
        return {0, 0};
    }

    uint32_t c = length == 1 ? lead : lead & (0x7f >> length);
    for (std::size_t i = 1; i < length; i++)
    {
        if ((byte(i) & 0xc0) != 0x80)
        {
            return {0, 0};
        }

        c = c << 6 | (byte(i) & 0x3f);
    }

    // Reject overlong encodings, UTF-16 surrogates and code points above U+10FFFF.
    constexpr uint32_t kShortest[] = {0, 0, 0x80, 0x800, 0x10000};
    if (c < kShortest[length] || (c >= 0xd800 && c < 0xe000) || c > 0x10ffff)
    {
        return {0, 0};
    }

    return {c, length};
}

bool IsUtf8(std::string_view text)
{
    while (!text.empty())
    {
        const std::size_t length = Utf8Character(text).second;
        if (length == 0)
        {
            return false;
        }

        text.remove_prefix(length);
    }

    return true;
}

bool IsPrintable(uint32_t c)
{
    return c >= 0x20 && !(c >= 0x7f && c < 0xa0) && c != 0x2028 && c != 0x2029;
}

std::string PrintableName(std::string_view name)
{
    std::string printable;
    for (std::size_t i = 0; i < name.size();)
    {
        const auto [c, length] = Utf8Character(name.substr(i));
        printable += length == 0 || !IsPrintable(c) ? "?" : name.substr(i, length);
        i += std::max<std::size_t>(length, 1);
    }

    return printable;
}

} // namespace xsflib
