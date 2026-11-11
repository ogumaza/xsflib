// SPDX-License-Identifier: MIT

// Checks the helpers for the tests of PSF sets (xsflib/testing/sets.h): the readers of files in memory, the reads of a
// render's frames, the whole output of a playback, a render's peak, and the checks of a playback. Checks the checks of
// seeks in a whole track and of a player's snapshots (xsflib/testing/seeks.h) too. Each check passes on a right
// playback or player and fails on a wrong one.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "ramp_players.h"
#include "xsflib/player/playback.h"
#include "xsflib/testing/check.h"
#include "xsflib/testing/seeks.h"
#include "xsflib/testing/sets.h"

using ramp_players::RampPlayer;
using ramp_players::SnapshotRampPlayer;
using seqlib::testing::Capture;
using seqlib::testing::Captured;
using xsflib::testing::Check;
using xsflib::testing::CheckEnd;
using xsflib::testing::CheckLengthAndFade;
using xsflib::testing::CheckRefusals;
using xsflib::testing::CheckSeeks;
using xsflib::testing::CheckVolume;
using xsflib::testing::Peak;

namespace
{

void TestMapReader()
{
    const std::map<std::string, std::vector<uint8_t>> kFiles = {{"dir/lib.tst", {1, 2}}, {"dir/sub/x.minitst", {3}}};
    const xsflib::psf::FileReader reader = xsflib::testing::MapReader(kFiles);
    std::vector<uint8_t> data;

    XSFLIB_CHECK(reader("dir/lib.tst", data) && data == std::vector<uint8_t>({1, 2}));
    // A library's path from a mini file's folder resolves as it would on disk.
    XSFLIB_CHECK(reader("dir/sub/../lib.tst", data) && data == std::vector<uint8_t>({1, 2}));
    XSFLIB_CHECK(reader("dir/./sub/x.minitst", data) && data == std::vector<uint8_t>({3}));
    XSFLIB_CHECK(!reader("dir/missing.tst", data));
}

// The exact reader finds a path up from a folder and an archive's path for a file inside it. The reader looks both up
// unchanged.
void TestExactReader()
{
    const std::map<std::string, std::vector<uint8_t>> kFiles = {{"dir/sub/../lib.tst", {1, 2}},
                                                                {"unpack://zip|5|a.zip|x.minitst", {3}}};
    const xsflib::psf::FileReader reader = xsflib::testing::ExactReader(kFiles);
    std::vector<uint8_t> data;

    XSFLIB_CHECK(reader("dir/sub/../lib.tst", data) && data == std::vector<uint8_t>({1, 2}));
    XSFLIB_CHECK(reader("unpack://zip|5|a.zip|x.minitst", data) && data == std::vector<uint8_t>({3}));
    XSFLIB_CHECK(!reader("dir/lib.tst", data));
}

// The failures that `f`'s checks report. The test takes them back, and keeps their messages out of its output. A given
// `messages` gets the messages. An exception from `f` leaves the state of the checks as it was before `f`.
template <typename F>
int FailuresOf(F f, std::string* messages = nullptr)
{
    const Captured captured = Capture(f);
    if (messages != nullptr)
    {
        *messages = captured.text;
    }

    return captured.failures;
}

// A playback of kFrames frames whose samples count up from 1, left before right, or one that doesn't open. The playback
// of "late" lands a frame after each seek's frame.
class CountingPlayback
{
public:
    static constexpr std::size_t kFrames = 7;

    bool Open(const std::string& path, const xsflib::psf::FileReader&)
    {
        error_ = path == "broken" ? "it doesn't open" : "";
        late_ = path == "late";

        return error_.empty();
    }

    bool Start()
    {
        frame_ = 0;

        return true;
    }

    std::size_t Render(int16_t* out, std::size_t frames)
    {
        const std::size_t n = std::min<std::size_t>(frames, kFrames - frame_);
        for (std::size_t i = 0; i < 2 * n; i++)
        {
            out[i] = static_cast<int16_t>(2 * frame_ + i + 1);
        }

        frame_ += n;

        return n;
    }

    bool Seek(uint64_t frame)
    {
        if (frame >= kFrames)
        {
            return false;
        }

        frame_ = static_cast<std::size_t>(frame) + (late_ ? 1 : 0);

        return true;
    }

    uint64_t Position() const
    {
        return frame_;
    }

    std::string Error() const
    {
        return error_;
    }

private:
    std::size_t frame_ = 0;
    bool late_ = false;
    std::string error_;
};

// The samples of a CountingPlayback's first `frames` frames.
std::vector<int16_t> CountingSamples(std::size_t frames)
{
    std::vector<int16_t> samples;
    for (int16_t v = 1; v <= static_cast<int16_t>(2 * frames); v++)
    {
        samples.push_back(v);
    }

    return samples;
}

void TestRenderFrames()
{
    CountingPlayback playback;
    std::vector<std::size_t> asks;
    const auto read = [&playback, &asks](int16_t* out, std::size_t frames)
    {
        asks.push_back(frames);

        return playback.Render(out, frames);
    };

    // Renders from the start. `asks` gets the frames that each read asks for.
    const auto render = [&](std::size_t limit, std::size_t block)
    {
        playback.Start();
        asks.clear();

        return xsflib::testing::RenderFrames(read, limit, block);
    };

    const std::vector<int16_t> all = CountingSamples(CountingPlayback::kFrames);

    // The limit cuts the last read short.
    XSFLIB_CHECK(render(5, 2) == CountingSamples(5) && asks == std::vector<std::size_t>({2, 2, 1}));

    // Without a limit, the reads go on until one gives no frames.
    XSFLIB_CHECK(render(SIZE_MAX, 3) == all && asks == std::vector<std::size_t>({3, 3, 3, 3}));

    // A limit past the end, and a limit of 0.
    XSFLIB_CHECK(render(100, 1024) == all && asks == std::vector<std::size_t>({100, 93}));
    XSFLIB_CHECK(render(0, 1024).empty() && asks.empty());

    // By default, there's no limit, and the reads ask for 1,024 frames.
    playback.Start();
    asks.clear();

    XSFLIB_CHECK(xsflib::testing::RenderFrames(read) == all && asks == std::vector<std::size_t>({1024, 1024}));
}

void TestRenderAll()
{
    const std::vector<int16_t> expected = CountingSamples(CountingPlayback::kFrames);

    // Blocks that divide the playback and blocks that don't give the same output.
    XSFLIB_CHECK(xsflib::testing::RenderAll<CountingPlayback>("song", {}, 3) == expected);
    XSFLIB_CHECK(xsflib::testing::RenderAll<CountingPlayback>("song", {}, 7) == expected);
    XSFLIB_CHECK(xsflib::testing::RenderAll<CountingPlayback>("song", {}) == expected);

    // A playback that doesn't open gives nothing, and fails one check that names the playback's error.
    bool empty = false;
    std::string messages;
    const auto render = [&]
    {
        empty = xsflib::testing::RenderAll<CountingPlayback>("broken", {}).empty();
    };

    const int found = FailuresOf(render, &messages);

    XSFLIB_CHECK(empty && found == 1 && messages.find("broken plays: it doesn't open") != std::string::npos);
}

// A model's output of `frames` stereo frames, with samples that differ from each other and from 0.
std::vector<int16_t> Model(std::size_t frames)
{
    std::vector<int16_t> model(2 * frames);
    for (std::size_t i = 0; i < model.size(); i++)
    {
        model[i] = static_cast<int16_t>(i % 2 == 0 ? 1000 + 3 * i : -999 - 5 * i);
    }

    return model;
}

void TestLengthAndFade()
{
    const std::vector<int16_t> model = Model(12);
    std::vector<int16_t> played(model.begin(), model.begin() + 2 * 8);
    for (std::size_t i = 0; i < 2 * 4; i++)
    {
        const double gain = 1.0 - static_cast<double>(i / 2) / 4.0;
        played.push_back(static_cast<int16_t>(std::round(model[2 * 8 + i] * gain)));
    }

    std::vector<int16_t> unfaded = played;
    unfaded[2 * 10] = model[2 * 10];
    std::vector<int16_t> wrong = played;
    wrong[3]++;
    const std::vector<int16_t> short_fade(played.begin(), played.end() - 2);

    XSFLIB_CHECK(FailuresOf([&] { CheckLengthAndFade(played, model, 8, 4); }) == 0);
    XSFLIB_CHECK(FailuresOf([&] { CheckLengthAndFade(unfaded, model, 8, 4); }) == 1);
    XSFLIB_CHECK(FailuresOf([&] { CheckLengthAndFade(wrong, model, 8, 4); }) == 1);
    XSFLIB_CHECK(FailuresOf([&] { CheckLengthAndFade(short_fade, model, 8, 4); }) == 2);
}

void TestEnd()
{
    const std::vector<int16_t> model = Model(20);
    const std::vector<int16_t> played(model.begin(), model.begin() + 2 * (5 + 3));
    std::vector<int16_t> wrong = played;
    wrong.back()++;

    XSFLIB_CHECK(FailuresOf([&] { CheckEnd(played, model, 5, 3); }) == 0);
    XSFLIB_CHECK(FailuresOf([&] { CheckEnd(played, model, 6, 3); }) == 1);
    XSFLIB_CHECK(FailuresOf([&] { CheckEnd(wrong, model, 5, 3); }) == 1);
}

void TestVolume()
{
    const std::vector<int16_t> kPlayed = {100, -100, 3, -3, 32767, -32768};
    const std::vector<int16_t> kHalved = {50, -50, 2, -2, 16384, -16384};

    XSFLIB_CHECK(FailuresOf([&] { CheckVolume(kHalved, kPlayed, 0.5); }) == 0);
    XSFLIB_CHECK(FailuresOf([&] { CheckVolume(kPlayed, kPlayed, 0.5); }) == 1);
    XSFLIB_CHECK(FailuresOf([&] { CheckVolume({50, -50}, kPlayed, 0.5); }) == 1);
}

void TestPeak()
{
    const std::vector<int16_t> kSamples = {100, -3, 7, -250, 4};

    XSFLIB_CHECK(Peak(kSamples) == 250 && Peak({}) == 0 && Peak(std::vector<int16_t>(4, 0)) == 0);
    XSFLIB_CHECK(Peak(std::vector<int16_t>{-32768, 5}) == 32768 && Peak(std::vector<int16_t>{32767}) == 32767);
    XSFLIB_CHECK(Peak(std::span<const int16_t>(kSamples).first(3)) == 100);
}

void TestSeeks()
{
    const std::vector<int16_t> straight = xsflib::testing::RenderAll<CountingPlayback>("song", {});
    std::vector<int16_t> wrong = straight;
    wrong[2 * 1]++;

    // A seek forward and a seek back, rendering 2 frames after each.
    XSFLIB_CHECK(FailuresOf([&] { CheckSeeks<CountingPlayback>("song", {}, straight, {4, 1}, 2); }) == 0);
    XSFLIB_CHECK(FailuresOf([&] { CheckSeeks<CountingPlayback>("late", {}, straight, {4, 1}, 2); }) == 1);
    XSFLIB_CHECK(FailuresOf([&] { CheckSeeks<CountingPlayback>("song", {}, wrong, {4, 1}, 2); }) == 1);

    // A seek past the end fails. So does one that leaves too few frames to render.
    XSFLIB_CHECK(FailuresOf([&] { CheckSeeks<CountingPlayback>("song", {}, straight, {9}, 2); }) == 1);
    XSFLIB_CHECK(FailuresOf([&] { CheckSeeks<CountingPlayback>("song", {}, straight, {6}, 2); }) == 1);
}

void TestRefusals()
{
    XSFLIB_CHECK(FailuresOf([] { CheckRefusals<CountingPlayback>({{"broken", "doesn't open"}}, {}); }) == 0);
    XSFLIB_CHECK(FailuresOf([] { CheckRefusals<CountingPlayback>({{"broken", "is broken"}}, {}); }) == 1);
    XSFLIB_CHECK(FailuresOf([] { CheckRefusals<CountingPlayback>({{"song", ""}}, {}); }) == 1);

    // A set that opens fails the check, even after a set that doesn't open.
    const auto one_opens = []
    {
        CheckRefusals<CountingPlayback>({{"broken", "doesn't open"}, {"song", "doesn't open"}}, {});
    };

    XSFLIB_CHECK(FailuresOf(one_opens) == 1);
}

// A started playback of a ramp of 8 s and a 0.5 s fade at 1,000 Hz: 8,500 frames.
template <typename Player>
std::unique_ptr<xsflib::player::Playback<Player>> StartedRamp()
{
    auto playback = std::make_unique<xsflib::player::Playback<Player>>();
    playback->Open(8000, {});
    playback->Start();

    return playback;
}

// The whole output of StartedRamp.
template <typename Player>
std::vector<int16_t> RampTrack()
{
    const std::unique_ptr<xsflib::player::Playback<Player>> playback = StartedRamp<Player>();
    const auto render = [&playback](int16_t* out, std::size_t frames)
    {
        return playback->Render(out, frames);
    };

    return xsflib::testing::RenderFrames(render);
}

// The seek checks of whole tracks pass on the track, with a buffer for the whole track and with a buffer for a part of
// it. A wrong first sample fails the render to the end and the seek to the start.
template <typename Player>
void TestSeekChecks(const std::string& what)
{
    const std::vector<int16_t> track = RampTrack<Player>();
    XSFLIB_REQUIRE(track.size() == 2 * 8500);
    std::vector<int16_t> wrong = track;
    wrong[0]++;

    const auto seeking = [](const std::vector<int16_t>& expected)
    {
        const std::unique_ptr<xsflib::player::Playback<Player>> playback = StartedRamp<Player>();
        xsflib::testing::CheckSeeking(*playback, expected, "seeking");
    };

    const auto ahead = [](const std::vector<int16_t>& expected, uint64_t keep)
    {
        xsflib::testing::CheckRenderAhead(StartedRamp<Player>(), expected, keep, "rendering ahead");
    };

    Check(FailuresOf([&] { seeking(track); }) == 0, what + ": seeks in the track pass");
    Check(FailuresOf([&] { seeking(wrong); }) == 2, what + ": a wrong first sample fails two seek checks");
    Check(FailuresOf([&] { ahead(track, 1000000); }) == 0, what + ": rendering ahead of the whole track passes");
    Check(FailuresOf([&] { ahead(track, 1000); }) == 0, what + ": rendering ahead of a part of the track passes");
    Check(FailuresOf([&] { ahead(wrong, 1000000); }) == 2,
          what + ": a wrong first sample fails two checks of rendering ahead");
}

// A ramp player whose snapshots hold the player and the run that took them. It refuses an earlier run's snapshot, and
// another player's unless `accepts_others_` is set. A restore lands `late_` frames after the snapshot's frame.
class OwnedSnapshotPlayer : public RampPlayer
{
public:
    struct Snapshot
    {
        const OwnedSnapshotPlayer* player = nullptr;
        int run = 0;
        uint64_t position = 0;
    };

    std::shared_ptr<const Snapshot> Save(const Snapshot*)
    {
        return std::make_shared<const Snapshot>(Snapshot{this, starts_, position_});
    }

    bool Restore(const Snapshot& snapshot)
    {
        if ((snapshot.player != this && !accepts_others_) || snapshot.run != starts_)
        {
            return false;
        }

        position_ = snapshot.position + late_;
        return true;
    }

    bool accepts_others_ = false;
    uint64_t late_ = 0;
};

// The snapshot checks pass on a right player. A player whose restores land a frame late fails the four checks of the
// output after a restore. A player that takes another player's snapshot fails the check of that refusal, and only when
// the check is asked for.
void TestSnapshotChecks()
{
    const auto failures = [](uint64_t late, bool accepts_others, bool other_player_refuses)
    {
        return FailuresOf(
            [&]
            {
                OwnedSnapshotPlayer reference;
                OwnedSnapshotPlayer player;
                reference.accepts_others_ = accepts_others;
                player.late_ = late;
                reference.Start();
                player.Start();
                xsflib::testing::CheckSnapshots(reference, player, 3000, "snapshots", other_player_refuses, 256);
            });
    };

    XSFLIB_CHECK(failures(0, false, true) == 0);
    XSFLIB_CHECK(failures(1, false, true) == 4);
    XSFLIB_CHECK(failures(0, true, true) == 1);
    XSFLIB_CHECK(failures(0, true, false) == 0);
}

} // namespace

int main()
{
    const auto tests = []
    {
        TestMapReader();
        TestExactReader();
        TestRenderFrames();
        TestRenderAll();
        TestLengthAndFade();
        TestEnd();
        TestVolume();
        TestPeak();
        TestSeeks();
        TestRefusals();
        TestSeekChecks<RampPlayer>("without snapshots");
        TestSeekChecks<SnapshotRampPlayer>("with snapshots");
        TestSnapshotChecks();
    };

    return xsflib::testing::RunTest("testing sets", tests);
}
