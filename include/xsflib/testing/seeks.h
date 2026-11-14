// SPDX-License-Identifier: MIT

// Checks of the seeks in a whole track against an uninterrupted render of it: the seeks of a Playback, the reads and
// seeks while a RenderAhead renders the track ahead, and a player's snapshots. A test that includes this header links
// xsflib::player.

#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <ratio>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "xsflib/player/playback.h"
#include "xsflib/player/render_ahead.h"
#include "xsflib/testing/check.h"
#include "xsflib/testing/sets.h"

namespace xsflib::testing
{

// The frames that the checks render or read after each seek.
inline constexpr std::size_t kFramesAfterSeek = 5000;

// The frames that the seek checks go to in a track of `total` frames, in turn: back, forward within what has been
// rendered, to the start and to the end, from the current position, and past the end.
inline std::vector<std::size_t> SeekTargets(std::size_t total)
{
    return {total / 2, total / 4 + 333, total * 3 / 4, 0, total - 777, total - 777 + 50, 1, total / 3, total + 1000};
}

// The stereo frames of `pcm` from frame `from` on: `count` frames, or fewer at the end of `pcm`.
inline std::vector<int16_t> FramesOf(const std::vector<int16_t>& pcm, std::size_t from, std::size_t count)
{
    const std::size_t frames = pcm.size() / 2;
    const std::size_t first = std::min(from, frames);
    const std::size_t last = first + std::min(count, frames - first);

    return {pcm.begin() + static_cast<std::ptrdiff_t>(2 * first), pcm.begin() + static_cast<std::ptrdiff_t>(2 * last)};
}

namespace detail
{

// A duration in milliseconds, for the times that the checks print.
inline double Milliseconds(std::chrono::steady_clock::duration duration)
{
    return std::chrono::duration<double, std::milli>(duration).count();
}

} // namespace detail

// Checks the seeks of `playback`, a Playback that has just started, against `expected`, the whole output of an
// uninterrupted render of the same track. A render to the end has to give `expected`. A playback that takes snapshots
// takes them in that render. Then each seek to one of SeekTargets has to land at its frame, or at the end of the track,
// and the next kFramesAfterSeek frames have to be those of `expected`. `what` names the case in the failures. The
// count of snapshots and the times of the seeks go to stderr.
template <typename Playback>
void CheckSeeking(Playback& playback, const std::vector<int16_t>& expected, const std::string& what)
{
    using Clock = std::chrono::steady_clock;

    const std::size_t total = expected.size() / 2;
    const auto render = [&playback](int16_t* out, std::size_t frames)
    {
        return playback.Render(out, frames);
    };

    Check(RenderFrames(render) == expected, what + ": a render to the end matches the uninterrupted render");

    const auto start = Clock::now();
    double longest = 0;
    for (const std::size_t target : SeekTargets(total))
    {
        const auto seek_start = Clock::now();
        const bool sought = playback.Seek(target);
        longest = std::max(longest, detail::Milliseconds(Clock::now() - seek_start));

        const std::size_t at = std::min(target, total);
        Check(sought && playback.Position() == at,
              what + ": a seek to " + std::to_string(target) + " landed at " + std::to_string(playback.Position()));
        Check(RenderFrames(render, kFramesAfterSeek) == FramesOf(expected, at, kFramesAfterSeek),
              what + ": the samples after a seek to " + std::to_string(target) + " match the uninterrupted render");
    }

    std::fprintf(stderr, "%s: %zu snapshots, seeks took %.0f ms at most, %.0f ms in all\n", what.c_str(),
                 playback.SnapshotCount(), longest, detail::Milliseconds(Clock::now() - start));
}

// Checks rendering ahead of `playback`, a Playback that has just started, against `expected`, the whole output of an
// uninterrupted render of the same track. The buffer holds at most `keep` frames. The reads follow a seek ahead of the
// buffer, a seek back while the worker renders towards a target, a seek within the buffer and one before it, and then
// a seek to each of SeekTargets. After each seek, the frames read have to be those of `expected`. `what` names the case
// in the failures. The time that the checks took and the frames in the buffer at the end go to stderr.
template <typename Player>
void CheckRenderAhead(std::unique_ptr<player::Playback<Player>> playback, const std::vector<int16_t>& expected,
                      uint64_t keep, const std::string& what)
{
    using Clock = std::chrono::steady_clock;

    const std::size_t total = expected.size() / 2;
    const auto start = Clock::now();
    player::RenderAhead ahead(std::move(playback), keep);
    const auto read = [&ahead](int16_t* out, std::size_t frames)
    {
        return ahead.Read(out, frames);
    };

    // Seeks to `target` and reads up to `frames` frames, or to the end of the track.
    const auto read_at = [&](std::size_t target, std::size_t frames)
    {
        ahead.Seek(target);
        const std::size_t at = std::min(target, total);
        Check(RenderFrames(read, frames) == FramesOf(expected, at, frames),
              what + ": the samples after a seek to " + std::to_string(target) + " match the uninterrupted render");
    };

    read_at(total * 3 / 4, kFramesAfterSeek);
    ahead.Seek(total / 2);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    read_at(total / 8, kFramesAfterSeek);
    read_at(0, SIZE_MAX);
    Check(ahead.Error().empty(), what + ": no error (" + ahead.Error() + ")");

    for (const std::size_t target : SeekTargets(total))
    {
        read_at(target, kFramesAfterSeek);
    }

    const player::RenderAhead::Kept kept = ahead.GetKept();
    std::fprintf(stderr, "%s: took %.0f ms, buffered frames %llu to %llu\n", what.c_str(),
                 detail::Milliseconds(Clock::now() - start), static_cast<unsigned long long>(kept.start),
                 static_cast<unsigned long long>(kept.end));
}

namespace detail
{

// The whole output of an uninterrupted render of a set: a Playback with `options` but no snapshots, opened from
// `source` (see CheckSeekingOfSet). The playback has to start. Unless `require_sound` is false, it also has to render
// something and end without an error. `what` names the case in the failures.
template <typename Player, typename... Source>
std::vector<int16_t> ReferenceRender(const player::PlaybackOptions<Player>& options, bool require_sound,
                                     const std::string& what, const Source&... source)
{
    player::PlaybackOptions<Player> straight = options;
    straight.snapshots = false;
    player::Playback<Player> reference;
    Check(reference.Open(source..., straight) && reference.Start(), what + ": the reference starts");
    const std::vector<int16_t> expected =
        RenderFrames([&reference](int16_t* out, std::size_t frames) { return reference.Render(out, frames); });
    if (require_sound)
    {
        Check(!expected.empty() && reference.GetPlayer().GetState() != Player::State::kError,
              what + ": the reference renders");
    }

    return expected;
}

} // namespace detail

// Checks the seeks of a Playback of a set with `options` against an uninterrupted render of the same set without
// snapshots (CheckSeeking). `source` is what Playback::Open takes before the options: a path and a reader, or a set.
// The reference has to start, render something and end without an error, and the playback has to start. `what` names
// the case in the failures.
template <typename Player, typename... Source>
void CheckSeekingOfSet(const player::PlaybackOptions<Player>& options, const std::string& what, const Source&... source)
{
    const std::vector<int16_t> expected = detail::ReferenceRender(options, true, what, source...);

    player::Playback<Player> playback;
    if (!playback.Open(source..., options) || !playback.Start())
    {
        Check(false, what + ": playback starts (" + playback.Error() + ")");
        return;
    }

    CheckSeeking(playback, expected, what);
}

// Checks rendering ahead of a Playback of a set with `options`, with a buffer of at most `keep` frames, against an
// uninterrupted render of the same set without snapshots (CheckRenderAhead). `source` is what Playback::Open takes
// before the options. The reference and the playback have to start. `what` names the case in the failures.
template <typename Player, typename... Source>
void CheckRenderAheadOfSet(const player::PlaybackOptions<Player>& options, uint64_t keep, const std::string& what,
                           const Source&... source)
{
    const std::vector<int16_t> expected = detail::ReferenceRender(options, false, what, source...);

    auto playback = std::make_unique<player::Playback<Player>>();
    if (!playback->Open(source..., options) || !playback->Start())
    {
        Check(false, what + ": playback starts (" + playback->Error() + ")");
        return;
    }

    CheckRenderAhead(std::move(playback), expected, keep, what);
}

// Checks the snapshots of `player` against `reference`, two players that have just started the same track. `reference`
// renders `frames` frames without a break. `player` renders them too. It takes a snapshot at 30 % of them and one at
// 60 %, the second on top of the first. Taking them mustn't change the output. Restoring each has to give the output
// from its frame on, and so does restoring the first again and taking a snapshot after that. A restarted player has to
// refuse an earlier run's snapshot. With `other_player_refuses`, `reference` has to refuse a snapshot of `player` too.
// The renders ask for `block` frames at a time. `what` names the case in the failures. The times that the snapshots
// took go to stderr.
template <typename Player>
void CheckSnapshots(Player& reference, Player& player, std::size_t frames, const std::string& what,
                    bool other_player_refuses, std::size_t block = 1024)
{
    using Clock = std::chrono::steady_clock;
    using Snapshot = typename Player::Snapshot;

    const std::size_t first = frames * 3 / 10 + 77; // not on a step boundary
    const std::size_t second = frames * 6 / 10 + 1001;
    const auto render = [block](Player& from, std::size_t count)
    {
        return RenderFrames([&from](int16_t* out, std::size_t n) { return from.Render(out, n); }, count, block);
    };

    const auto frames_from = [](const std::vector<int16_t>& pcm, std::size_t from, std::size_t to)
    {
        return FramesOf(pcm, from, to - from);
    };

    const std::vector<int16_t> expected = render(reference, frames);
    Check(expected.size() == frames * 2, what + ": the reference render is complete");

    // Taking snapshots must not change the audio.
    std::vector<int16_t> pcm = render(player, first);
    auto start = Clock::now();
    const std::shared_ptr<const Snapshot> one = player.Save(nullptr);
    const double save_first = detail::Milliseconds(Clock::now() - start);
    std::vector<int16_t> more = render(player, second - first);
    pcm.insert(pcm.end(), more.begin(), more.end());
    start = Clock::now();
    const std::shared_ptr<const Snapshot> two = player.Save(one.get());
    const double save_second = detail::Milliseconds(Clock::now() - start);
    more = render(player, frames - second);
    pcm.insert(pcm.end(), more.begin(), more.end());

    Check(one && two, what + ": snapshots taken (" + player.Error() + ")");
    Check(pcm == expected, what + ": taking snapshots preserves output");
    if (!one || !two)
    {
        return;
    }

    // Restore each snapshot and compare the remaining audio.
    start = Clock::now();
    Check(player.Restore(*one), what + ": restored the first snapshot");
    const double restore = detail::Milliseconds(Clock::now() - start);
    Check(render(player, frames - first) == frames_from(expected, first, frames),
          what + ": first snapshot reproduces output");
    Check(player.Restore(*two), what + ": restored the second snapshot");
    Check(render(player, frames - second) == frames_from(expected, second, frames),
          what + ": second snapshot reproduces output");

    // Restore the first snapshot again, then take a new snapshot that shares pages with the second.
    Check(player.Restore(*one), what + ": restored the first snapshot again");
    pcm = render(player, second - first);
    const std::shared_ptr<const Snapshot> three = player.Save(two.get());
    more = render(player, frames - second);
    pcm.insert(pcm.end(), more.begin(), more.end());
    Check(pcm == frames_from(expected, first, frames), what + ": first snapshot still reproduces output");
    Check(three && player.Restore(*three) && render(player, frames - second) == frames_from(expected, second, frames),
          what + ": snapshot taken after a restore reproduces output");

    // A snapshot belongs to its player and its run.
    if (other_player_refuses)
    {
        Check(!reference.Restore(*one), what + ": another player refuses the snapshot");
    }

    Check(player.Start() && !player.Restore(*one), what + ": a restarted player refuses an earlier run's snapshot");

    std::fprintf(stderr, "%s: snapshots took %.1f and %.1f ms, restoring %.1f ms\n", what.c_str(), save_first,
                 save_second, restore);
}

} // namespace xsflib::testing
