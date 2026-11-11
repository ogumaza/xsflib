// SPDX-License-Identifier: MIT

// Tests Playback (xsflib/player/playback.h) with two test players: one that takes snapshots and one that doesn't. Both
// render a ramp in which every sample shows the frame it came from. The tests cover the length and fade from the tags,
// the end of an untagged sound, seeks with and without snapshots, rendering ahead with each player, and a rate that
// depends on the set.

#include "xsflib/player/playback.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "ramp_players.h"
#include "xsflib/player/render_ahead.h"
#include "xsflib/testing/check.h"
#include "xsflib/testing/sets.h"

using namespace xsflib::player;
using ramp_players::RampPlayer;
using ramp_players::RampSample;
using ramp_players::SetRatePlayer;
using ramp_players::SnapshotRampPlayer;
using xsflib::testing::Check;

namespace
{

static_assert(!HasSnapshots<RampPlayer>);
static_assert(HasSnapshots<SnapshotRampPlayer>);

// The left samples of the frames that `read` gives, 64 at a time, until it gives none. `read` is a call that renders,
// such as Playback::Render.
template <typename Read>
std::vector<int16_t> LeftSamples(Read read)
{
    const std::vector<int16_t> pcm = xsflib::testing::RenderFrames(read, SIZE_MAX, 64);
    std::vector<int16_t> left;
    for (std::size_t i = 0; i < pcm.size(); i += 2)
    {
        left.push_back(pcm[i]);
    }

    return left;
}

// Renders the rest of `playback` and returns its frames' left samples.
template <typename Player>
std::vector<int16_t> RenderAll(Playback<Player>& playback)
{
    return LeftSamples([&playback](int16_t* out, std::size_t frames) { return playback.Render(out, frames); });
}

void TestLengthAndFade()
{
    Playback<RampPlayer> playback;
    playback.Open(1000, {});
    playback.Start();

    const std::vector<int16_t> left = RenderAll(playback);

    Check(playback.HasLengthTag() && playback.LengthFrames() == 1500, "1 s and a 0.5 s fade at 1,000 Hz");
    XSFLIB_REQUIRE(left.size() == 1500);
    Check(left[999] == RampSample(999), "no fade before the fade");
    Check(left[1250] == RampSample(1250) / 2, "half way through the fade");
}

void TestUntaggedEnd()
{
    Playback<RampPlayer> playback;
    playback.Open(-1, {});
    playback.Start();

    const std::vector<int16_t> left = RenderAll(playback);

    // The sound reports its end at frame 300 (FinishedFrame). Playback ends half a second later.
    Check(!playback.HasLengthTag() && left.size() == 800, "an untagged sound ends 0.5 s after it finishes");
}

// The length for a file's tags, with the default options. Without a length tag, a file plays for 150 s, and fades for
// 10 s unless it has a fade tag. A file with a length tag and no fade tag has no fade.
void TestTagLength()
{
    const PlaybackOptions<RampPlayer> kOptions;
    const auto length_is = [&kOptions](const xsflib::psf::Tags& tags, long long play_ms, long long fade_ms)
    {
        const TrackLength length = TagLength(tags, kOptions);

        return length.play_ms == play_ms && length.fade_ms == fade_ms;
    };

    XSFLIB_CHECK(length_is({{"length", "1:00"}, {"fade", "5"}}, 60000, 5000));
    XSFLIB_CHECK(length_is({{"length", "1:00"}}, 60000, 0));
    XSFLIB_CHECK(length_is({}, 150000, 10000));
    XSFLIB_CHECK(length_is({{"fade", "5"}}, 150000, 5000));
}

void TestSeekWithoutSnapshots()
{
    Playback<RampPlayer> playback;
    playback.Open(1000, {});
    playback.Start();
    int16_t pcm[2 * 100];
    for (int i = 0; i < 10; i++)
    {
        playback.Render(pcm, 100);
    }

    XSFLIB_CHECK(playback.SeekStart(250) == 0);
    XSFLIB_CHECK(playback.Seek(250));
    playback.Render(pcm, 1);

    XSFLIB_CHECK(playback.GetPlayer().starts_ == 2);
    XSFLIB_CHECK(playback.SnapshotCount() == 0);
    XSFLIB_CHECK(pcm[0] == RampSample(250));
}

void TestSeekWithSnapshots()
{
    PlaybackOptions<SnapshotRampPlayer> options;
    options.snapshot_interval = 100;
    Playback<SnapshotRampPlayer> playback;
    playback.Open(1000, options);
    playback.Start();
    int16_t pcm[2 * 100];
    for (int i = 0; i < 10; i++)
    {
        playback.Render(pcm, 100);
    }

    XSFLIB_CHECK(playback.SeekStart(250) == 200);
    XSFLIB_CHECK(playback.Seek(250));
    playback.Render(pcm, 1);

    XSFLIB_CHECK(playback.GetPlayer().starts_ == 1);
    XSFLIB_CHECK(playback.GetPlayer().restores_ == 1);
    XSFLIB_CHECK(playback.SnapshotCount() > 0);
    XSFLIB_CHECK(pcm[0] == RampSample(250));
}

// A seek back moves the ends of the render blocks to other frames. A snapshot at the end of a block can then come
// earlier in a stretch than the snapshot already there. The earlier snapshot replaces the later one. Each stretch keeps
// one snapshot: the earliest.
void TestSnapshotsAfterSeekBack()
{
    PlaybackOptions<SnapshotRampPlayer> options;
    options.snapshot_interval = 100;
    Playback<SnapshotRampPlayer> playback;
    playback.Open(1000, options);
    playback.Start();
    int16_t pcm[2 * 64];
    for (int i = 0; i < 15; i++)
    {
        playback.Render(pcm, 64);
    }

    // Snapshots at 0, 128, 256, 320, 448, 512, 640, 704, 832 and 960.
    const std::size_t first_pass = playback.SnapshotCount();
    Check(first_pass == 10, "one snapshot in each stretch of 100 frames");

    // Back to 50, then on in blocks of 60: the blocks end at 110, 170, 230 and so on.
    XSFLIB_CHECK(playback.Seek(50));
    for (int i = 0; i < 15; i++)
    {
        playback.Render(pcm, 60);
    }

    Check(playback.SnapshotCount() == first_pass, "still one snapshot in each stretch after the seek back");
    Check(playback.SeekStart(120) == 110 && playback.SeekStart(240) == 230 && playback.SeekStart(330) == 320,
          "each stretch keeps its earliest snapshot");
}

// Reads all of a rendered-ahead playback of 1 s and a 0.5 s fade. It has to give `frames` frames, those of a straight
// render.
template <typename Player>
void TestRenderAhead(const std::string& what, std::size_t frames)
{
    Playback<Player> straight;
    straight.Open(1000, {});
    straight.Start();
    const std::vector<int16_t> expected = RenderAll(straight);

    auto playback = std::make_unique<Playback<Player>>();
    playback->Open(1000, {});
    playback->Start();
    RenderAhead ahead(std::move(playback));

    const std::vector<int16_t> left =
        LeftSamples([&ahead](int16_t* out, std::size_t count) { return ahead.Read(out, count); });

    Check(expected.size() == frames && left == expected, what + ": every frame matches a straight render");
    Check(ahead.Error().empty(), what + ": no error");
}

// The length follows the rate that Start settles.
void TestSetRateLength()
{
    Playback<SetRatePlayer<RampPlayer>> playback;
    playback.Open(1000, {});
    const uint64_t estimate = playback.LengthFrames();
    const int estimated_rate = playback.SampleRate();
    playback.Start();

    const std::vector<int16_t> left = RenderAll(playback);

    Check(estimated_rate == 1000 && estimate == 1500, "before the start, the length follows the estimate");
    Check(playback.SampleRate() == 2000 && playback.PlayFrames() == 2000 && playback.FadeFrames() == 1000,
          "after the start, the length follows the set's rate");
    XSFLIB_REQUIRE(left.size() == 3000);
    Check(left[1999] == RampSample(1999), "no fade before the fade");
    Check(left[2500] == RampSample(2500) / 2, "half way through the fade");
}

// An untagged sound ends half a second at the set's rate after it finishes.
void TestSetRateUntaggedEnd()
{
    Playback<SetRatePlayer<RampPlayer>> playback;
    playback.Open(-1, {});
    playback.Start();

    const std::vector<int16_t> left = RenderAll(playback);

    Check(left.size() == 1300, "an untagged sound ends 0.5 s at the set's rate after it finishes");
}

// The first snapshots come every 2 s at the set's rate, unless the options give another interval.
void TestSetRateSnapshots()
{
    int16_t pcm[2 * 1000];
    Playback<SetRatePlayer<SnapshotRampPlayer>> by_default;
    by_default.Open(100000, {});
    by_default.Start();
    for (int i = 0; i < 9; i++)
    {
        by_default.Render(pcm, 1000);
    }

    PlaybackOptions<SetRatePlayer<SnapshotRampPlayer>> options;
    options.snapshot_interval = 1000;
    Playback<SetRatePlayer<SnapshotRampPlayer>> chosen;
    chosen.Open(100000, options);
    chosen.Start();
    for (int i = 0; i < 9; i++)
    {
        chosen.Render(pcm, 1000);
    }

    // Frames 0, 4,000 and 8,000 by default, and each 1,000 frames with the option.
    Check(by_default.SnapshotCount() == 3, "2 s between snapshots at the set's rate");
    Check(chosen.SnapshotCount() == 10, "the options' interval between snapshots");
}

} // namespace

int main()
{
    const auto tests = []
    {
        TestLengthAndFade();
        TestUntaggedEnd();
        TestTagLength();
        TestSeekWithoutSnapshots();
        TestSeekWithSnapshots();
        TestSnapshotsAfterSeekBack();
        TestRenderAhead<RampPlayer>("without snapshots", 1500);
        TestRenderAhead<SnapshotRampPlayer>("with snapshots", 1500);
        TestSetRateLength();
        TestSetRateUntaggedEnd();
        TestSetRateSnapshots();

        // Rendering ahead takes its limit and block size from the playback's rate.
        TestRenderAhead<SetRatePlayer<RampPlayer>>("at the set's rate", 3000);
    };

    return xsflib::testing::RunTest("playback", tests);
}
