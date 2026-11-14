// SPDX-License-Identifier: MIT

// The text that the XMPlay input plugin (xsflib/xmplay/input.h) gives XMPlay: a file's tags as XMPlay's tags, the main
// window's format text, and the text of the General and Message info windows. PSF tags become XMPlay's tags (game as
// album, year as date).

#pragma once

#include <windows.h>
#include <xmpin.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>

#include "xsflib/player/playback.h"
#include "xsflib/psf/plugin_tags.h"
#include "xsflib/psf/psf.h"
#include "xsflib/utf8.h"

namespace xsflib::xmplay::detail
{

// The size limit of the main window's format text, with its null. The SDK gives no size for XMPlay's buffer.
constexpr std::size_t kFormatTextSize = 64;

// The most bytes that an info window's text takes. XMPlay's buffers hold about 40,000.
constexpr std::size_t kInfoLimit = 32000;

// A tag's value in XMPlay's text, which is UTF-8. PSF tags are UTF-8 when the file has a utf8 tag. Without one, text
// that isn't valid UTF-8 is read in the system's code page, through XMPlay's `text` and `misc` functions.
inline std::string NativeText(const std::string& value, bool utf8, XMPFUNC_TEXT* text, XMPFUNC_MISC* misc)
{
    if (utf8 || IsUtf8(value))
    {
        return value;
    }

    char* converted = text->Ansi(value.data(), static_cast<int>(value.size()));
    if (converted == nullptr)
    {
        return {};
    }

    std::string native = converted;
    misc->Free(converted);

    return native;
}

// XMPlay's tags for `tags` of the format `format_name`: the filetype tag, then the metadata, as names and values that
// each end with '\0'. `playback_tags` are the format's tags that aren't metadata. CopyForXmplay() adds the final '\0'.
template <typename PlaybackTags>
std::string TagBlock(const psf::Tags& tags, const char* format_name, const PlaybackTags& playback_tags,
                     XMPFUNC_TEXT* text, XMPFUNC_MISC* misc)
{
    const bool utf8 = tags.count("utf8") != 0;
    std::string block = "filetype";
    block += '\0';
    block += format_name;
    block += '\0';
    for (const auto& [name, value] : tags)
    {
        if (psf::IsHiddenTag(name, playback_tags) || name == "filetype")
        {
            continue;
        }

        block += psf::FieldFromTag(name);
        block += '\0';
        block += NativeText(value, utf8, text, misc);
        block += '\0';
    }

    return block;
}

// `text` and a final '\0' in memory from XMPlay's Alloc, which XMPlay frees, or nullptr when Alloc fails.
inline char* CopyForXmplay(const std::string& text, XMPFUNC_MISC* misc)
{
    auto* copy = static_cast<char*>(misc->Alloc(static_cast<DWORD>(text.size() + 1)));
    if (copy != nullptr)
    {
        std::memcpy(copy, text.c_str(), text.size() + 1);
    }

    return copy;
}

// Writes the main window's format text into `buffer`: the format's name and the sample rate.
inline void WriteFormatText(char* buffer, const char* format_name, int rate)
{
    std::snprintf(buffer, kFormatTextSize, "%s - %d Hz", format_name, rate);
}

// The General info window's text: lines of a heading and a value, separated by a tab. `tags` and `length` belong to
// the open file. `tags` is nullptr when no file is open.
inline std::string GeneralInfoText(const char* format_name, int rate, const psf::Tags* tags,
                                   const player::TrackLength& length)
{
    std::string text = "Format\t" + std::string(format_name) + "\r";
    text += "Sample rate\t" + std::to_string(rate) + " Hz\r";
    text += "Channels\t2\rResolution\t16 bit\r";
    if (tags != nullptr)
    {
        text += "Length\t" + psf::FormatTime(length.play_ms) + "\r";
        text += "Fade\t" + psf::FormatTime(length.fade_ms) + "\r";
        if (const auto it = tags->find("volume"); it != tags->end())
        {
            text += "Volume\t" + it->second + "\r";
        }
    }

    text.resize(std::min(text.size(), kInfoLimit - 1));
    return text;
}

// Writes the Message info window's text into `buffer`: `tags` by their PSF names, without the tags that aren't
// metadata (`playback_tags` among them). The text stops before the tag that would pass kInfoLimit.
template <typename PlaybackTags>
void WriteMessageInfo(char* buffer, const psf::Tags& tags, const PlaybackTags& playback_tags, XMPFUNC_TEXT* text,
                      XMPFUNC_MISC* misc)
{
    const bool utf8 = tags.count("utf8") != 0;
    char* end = buffer;
    for (const auto& [name, value] : tags)
    {
        if (psf::IsHiddenTag(name, playback_tags))
        {
            continue;
        }

        // FormatInfoText may write each line break as two characters.
        const std::string native = NativeText(value, utf8, text, misc);
        const std::size_t most = name.size() + 2 * native.size() + 4;
        if (static_cast<std::size_t>(end - buffer) + most >= kInfoLimit)
        {
            break;
        }

        end = misc->FormatInfoText(end, name.c_str(), native.c_str());
    }
}

} // namespace xsflib::xmplay::detail
