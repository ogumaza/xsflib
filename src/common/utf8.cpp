// SPDX-License-Identifier: MIT

// UTF-8 in the names and tags that files hold (see utf8.h).

#include "xsflib/utf8.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "xsflib/bytes.h"

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

void AppendUtf16(std::string& out, std::span<const uint8_t> text, const Utf16Rules& rules)
{
    const auto unit = [&](std::size_t at) -> uint32_t
    {
        return rules.big_endian ? LoadBe16(text.data() + at) : LoadLe16(text.data() + at);
    };

    const std::size_t start = out.size();
    for (std::size_t at = 0; at + 2 <= text.size(); at += 2)
    {
        uint32_t c = unit(at);
        if (c == 0 && rules.stop_at_nul)
        {
            break;
        }

        if (c >= 0xd800 && c < 0xdc00 && at + 4 <= text.size())
        {
            const uint32_t low = unit(at + 2);
            if (low >= 0xdc00 && low < 0xe000)
            {
                c = 0x10000 + ((c - 0xd800) << 10) + (low - 0xdc00);
                at += 2;
            }
        }

        // AppendUtf8 writes U+FFFD for a surrogate.
        if (c >= 0xd800 && c < 0xe000 && rules.lone_surrogate_bytes)
        {
            out += static_cast<char>(0xe0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3f));
            out += static_cast<char>(0x80 | (c & 0x3f));
        }
        else
        {
            AppendUtf8(out, c < 0x20 && rules.controls_as_spaces ? ' ' : c);
        }
    }

    if (rules.trim_spaces)
    {
        const std::size_t first = out.find_first_not_of(' ', start);
        if (first == std::string::npos)
        {
            out.resize(start);
            return;
        }

        out.resize(out.find_last_not_of(' ') + 1);
        out.erase(start, first - start);
    }
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
