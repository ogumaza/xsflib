// SPDX-License-Identifier: MIT

// Unit tests for the snapshot hooks of a player with two modes (xsflib/player/modes.h): the snapshot of the source that
// plays, the earlier snapshot that each save gets, a source whose Save returns its snapshot by value, which snapshots
// fit the source that plays, and the restore of each mode.

#include <memory>

#include "xsflib/player/modes.h"
#include "xsflib/testing/check.h"

using xsflib::player::CanRestoreModes;
using xsflib::player::RestoreModes;
using xsflib::player::SaveModes;
using xsflib::testing::Check;

namespace
{

// A source's state, and the snapshot that its Save got as the earlier one.
struct TestPart
{
    int value = 0;
    const TestPart* previous = nullptr;
};

// An archive mode's source whose Save returns its snapshot in a std::shared_ptr.
struct ArchiveSource
{
    std::shared_ptr<TestPart> Save(const TestPart* previous) const
    {
        return std::make_shared<TestPart>(TestPart{value, previous});
    }

    void Restore(const TestPart& part)
    {
        value = part.value;
    }

    int value = 0;
};

// A game mode's source whose Save returns its snapshot by value.
struct GameSource
{
    TestPart Save(const TestPart* previous) const
    {
        return {value, previous};
    }

    void Restore(const TestPart& part)
    {
        value = part.value;
    }

    int value = 0;
};

struct SourceSnapshot : xsflib::player::ModeSnapshot<TestPart, TestPart>
{
};

void CheckNeither()
{
    ArchiveSource* archive = nullptr;
    GameSource* game = nullptr;
    const SourceSnapshot* none = nullptr;
    SourceSnapshot both;
    both.archive = std::make_shared<TestPart>();
    both.game = std::make_shared<TestPart>();

    Check(SaveModes(archive, game, none) == nullptr, "nothing to save when neither mode plays");
    Check(!CanRestoreModes(archive, game, none), "no snapshot fits nothing");
    Check(!CanRestoreModes(archive, game, &both), "no snapshot fits when neither mode plays");
}

void CheckArchive()
{
    ArchiveSource archive{3};
    GameSource* game = nullptr;

    const std::shared_ptr<const SourceSnapshot> first =
        SaveModes(&archive, game, static_cast<SourceSnapshot*>(nullptr));

    Check(first && first->archive && !first->game, "archive mode saves the archive's snapshot alone");
    Check(first && first->archive && first->archive->value == 3 && first->archive->previous == nullptr,
          "the first archive snapshot holds the state and no earlier one");

    archive.value = 5;

    const std::shared_ptr<const SourceSnapshot> second = SaveModes(&archive, game, first.get());

    Check(second && second->archive && second->archive->previous == first->archive.get(),
          "the archive's Save gets the earlier archive snapshot");

    SourceSnapshot game_only;
    game_only.game = std::make_shared<TestPart>();

    Check(CanRestoreModes(&archive, game, first.get()), "an archive snapshot fits archive mode");
    Check(!CanRestoreModes(&archive, game, &game_only), "a game snapshot doesn't fit archive mode");

    RestoreModes(&archive, game, *first);

    Check(archive.value == 3, "archive mode restores the archive");
}

void CheckGame()
{
    ArchiveSource* archive = nullptr;
    GameSource game{7};

    const std::shared_ptr<const SourceSnapshot> first =
        SaveModes(archive, &game, static_cast<SourceSnapshot*>(nullptr));

    Check(first && first->game && !first->archive, "game mode saves the game's snapshot alone");
    Check(first && first->game && first->game->value == 7 && first->game->previous == nullptr,
          "a snapshot returned by value is kept");

    game.value = 9;

    const std::shared_ptr<const SourceSnapshot> second = SaveModes(archive, &game, first.get());

    Check(second && second->game && second->game->previous == first->game.get(),
          "the game's Save gets the earlier game snapshot");

    SourceSnapshot archive_only;
    archive_only.archive = std::make_shared<TestPart>();

    Check(CanRestoreModes(archive, &game, first.get()), "a game snapshot fits game mode");
    Check(!CanRestoreModes(archive, &game, &archive_only), "an archive snapshot doesn't fit game mode");

    RestoreModes(archive, &game, *first);

    Check(game.value == 7, "game mode restores the game");
}

} // namespace

int main()
{
    CheckNeither();
    CheckArchive();
    CheckGame();

    return xsflib::testing::Summary("player modes");
}
