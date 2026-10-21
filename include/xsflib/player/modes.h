// SPDX-License-Identifier: MIT

// The snapshot hooks of a player with two modes (see player.h). Archive mode plays a sound archive on a model of a
// console's sound library. Game mode runs a game's code on an emulation of the console. The player holds a source for
// each mode. At most one of them plays. The other is null. The player's SourceSnapshot derives from ModeSnapshot. The
// player's SaveSource, CanRestore and RestoreSource call SaveModes, CanRestoreModes and RestoreModes. Each source
// provides:
//
//   Save(previous)          returns its snapshot, in a std::shared_ptr or by value. `previous` is null or an earlier
//                           snapshot. The new snapshot may share what hasn't changed with it. It may throw.
//   Restore(snapshot)       returns the source to `snapshot`. It may throw.

#pragma once

#include <memory>
#include <type_traits>
#include <utility>

namespace xsflib::player
{

// The source's part of a snapshot: the snapshot of the source that played. The other source's snapshot is null.
template <typename ArchivePart, typename GamePart>
struct ModeSnapshot
{
    std::shared_ptr<ArchivePart> archive;
    std::shared_ptr<GamePart> game;
};

namespace detail
{

// A source's snapshot in a std::shared_ptr, whether the source's Save returns one or a value.
template <typename Part, typename Source>
std::shared_ptr<Part> SavePart(Source& source, const Part* previous)
{
    auto saved = source.Save(previous);
    if constexpr (std::is_convertible_v<decltype(saved), std::shared_ptr<Part>>)
    {
        return saved;
    }
    else
    {
        return std::make_shared<Part>(std::move(saved));
    }
}

} // namespace detail

// SaveSource's work: the snapshot of `archive` or `game`, whichever plays. The new part may share what hasn't changed
// with `previous`. `previous` is null or an earlier part. Returns nullptr when neither plays.
template <typename SourceSnapshot, typename Archive, typename Game>
std::shared_ptr<const SourceSnapshot> SaveModes(Archive* archive, Game* game, const SourceSnapshot* previous)
{
    auto source = std::make_shared<SourceSnapshot>();
    if (archive)
    {
        source->archive = detail::SavePart(*archive, previous ? previous->archive.get() : nullptr);
    }
    else if (game)
    {
        source->game = detail::SavePart(*game, previous ? previous->game.get() : nullptr);
    }
    else
    {
        return nullptr;
    }

    return source;
}

// CanRestore's work: whether `source` holds a snapshot of the source that plays.
template <typename SourceSnapshot, typename Archive, typename Game>
bool CanRestoreModes(const Archive* archive, const Game* game, const SourceSnapshot* source)
{
    return source != nullptr && (archive ? source->archive != nullptr : game != nullptr && source->game != nullptr);
}

// RestoreSource's work: returns the source that plays to its snapshot in `source`. CanRestoreModes has checked that the
// snapshot is there.
template <typename SourceSnapshot, typename Archive, typename Game>
void RestoreModes(Archive* archive, Game* game, const SourceSnapshot& source)
{
    if (archive)
    {
        archive->Restore(*source.archive);
    }
    else
    {
        game->Restore(*source.game);
    }
}

} // namespace xsflib::player
