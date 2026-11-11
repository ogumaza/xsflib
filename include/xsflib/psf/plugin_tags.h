// SPDX-License-Identifier: MIT

// The tags as the foobar2000 and XMPlay templates show them (xsflib/foobar2000/input.h and xsflib/xmplay/input.h): the
// names that foobar2000 and XMPlay give two PSF tags, and the tags that the plugins leave out.

#pragma once

#include <algorithm>
#include <string_view>

#include "xsflib/psf/psf.h"

namespace xsflib::psf
{

// The field for a PSF tag in foobar2000 and XMPlay: game is album, and year is date. Other tags keep their names.
inline std::string_view FieldFromTag(std::string_view tag)
{
    if (tag == "game")
    {
        return "album";
    }

    if (tag == "year")
    {
        return "date";
    }

    return tag;
}

// The PSF tag for a field in foobar2000 or XMPlay: album is game, and date is year. Other fields keep their names.
inline std::string_view TagFromField(std::string_view field)
{
    if (field == "album")
    {
        return "game";
    }

    if (field == "date")
    {
        return "year";
    }

    return field;
}

// Whether a plugin leaves the tag `name` out of the metadata: a PSF tag that isn't metadata (IsSystemTag), or one of
// `playback_tags`, the format's tags that set how a file plays (the traits' kPlaybackTags).
template <typename PlaybackTags>
bool IsHiddenTag(std::string_view name, const PlaybackTags& playback_tags)
{
    const auto named = [name](const char* tag)
    {
        return name == tag;
    };

    return IsSystemTag(name) || std::any_of(playback_tags.begin(), playback_tags.end(), named);
}

} // namespace xsflib::psf
