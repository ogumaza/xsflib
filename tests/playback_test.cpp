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

#include "xsflib/player/render_ahead.h"
#include "xsflib/testing/check.h"

using namespace xsflib::player;
using xsflib::testing::Check;

namespace
{

// The sample that a ramp player renders at `frame`.
int16_t RampSample(uint64_t frame)
{
    return static_cast<int16_t>(frame % 20000);
}

// A player without snapshots. It renders RampSample, and reports that it finished at `finish_at_`.
class RampPlayer
{
public:
    enum class State
    {
        kIdle,
        kPlaying,
        kFinished,
        kError,
    };

    static constexpr int kSampleRate = 1000;

    bool Load(int length_ms)
    {
        length_ms_ = length_ms;

        return true;
    }

    bool Start()
    {
        starts_++;
        position_ = 0;
        state_ = State::kPlaying;

        return true;
    }

    std::size_t Render(int16_t* out, std::size_t frames)
    {
        for (std::size_t i = 0; i < frames; i++)
        {
            out[2 * i] = out[2 * i + 1] = RampSample(position_ + i);
        }

        position_ += frames;
        if (position_ >= finish_at_ && state_ == State::kPlaying)
        {
            state_ = State::kFinished;
        }

        return frames;
    }

    long long LengthMs() const
    {
        return length_ms_;
    }

    long long FadeMs() const
    {
        return length_ms_ >= 0 ? 500 : -1;
    }

    double Volume() const
    {
        return 1.0;
    }

    State GetState() const
    {
        return state_;
    }

    uint64_t FinishedFrame() const
    {
        return finish_at_;
    }

    const std::string& Error() const
    {
        return error_;
    }

    int starts_ = 0;
    uint64_t finish_at_ = 300;

protected:
    uint64_t position_ = 0;

private:
    long long length_ms_ = 1000;
    State state_ = State::kIdle;
    std::string error_;
};

// A ramp player that takes snapshots: each holds the frame it was taken at.
class SnapshotRampPlayer : public RampPlayer
{
public:
    struct Snapshot
    {
        uint64_t position = 0;
    };

    std::shared_ptr<const Snapshot> Save(const Snapshot*)
    {
        return std::make_shared<const Snapshot>(Snapshot{position_});
    }

    bool Restore(const Snapshot& snapshot)
    {
        restores_++;
        position_ = snapshot.position;

        return true;
    }

    int restores_ = 0;
};

// A ramp player whose rate depends on the set: an estimate of 1,000 Hz until it starts, and 2,000 Hz after that.
template <typename Base>
class SetRatePlayer : public Base
{
public:
    static constexpr int kSampleRate = 0;

    int SampleRate() const
    {
        return Base::GetState() == Base::State::kIdle ? 1000 : 2000;
    }
};

static_assert(!HasSnapshots<RampPlayer>);
static_assert(HasSnapshots<SnapshotRampPlayer>);

// Renders the rest of `playback` and returns its frames' left samples.
template <typename Player>
std::vector<int16_t> RenderAll(Playback<Player>& playback)
{
    std::vector<int16_t> left;
    int16_t pcm[2 * 64];
    while (const std::size_t n = playback.Render(pcm, 64))
    {
        for (std::size_t i = 0; i < n; i++)
        {
            left.push_back(pcm[2 * i]);
        }
    }

    return left;
}

void TestLengthAndFade()
{
    Playback<RampPlayer> playback;
    playback.Open(1000, {});
    playback.Start();

    const std::vector<int16_t> left = RenderAll(playback);

    Check(playback.HasLengthTag() && playback.LengthFrames() == 1500, "1 s and a 0.5 s fade at 1,000 Hz");
    Check(left.size() == 1500, "the track's frames");
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

// Reads all of a rendered-ahead playback and checks every sample against the ramp and the fade.
template <typename Player>
void TestRenderAhead(const std::string& what)
{
    auto playback = std::make_unique<Playback<Player>>();
    playback->Open(1000, {});
    playback->Start();
    RenderAhead ahead(std::move(playback));

    std::vector<int16_t> left;
    int16_t pcm[2 * 64];
    while (const std::size_t n = ahead.Read(pcm, 64))
    {
        for (std::size_t i = 0; i < n; i++)
        {
            left.push_back(pcm[2 * i]);
        }
    }

    Check(left.size() == 1500 && left[500] == RampSample(500), what + ": every frame");
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
    Check(left.size() == 3000, "the track's frames at the set's rate");
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

// Rendering ahead takes its limit and block size from the playback's rate.
void TestSetRateRenderAhead()
{
    auto playback = std::make_unique<Playback<SetRatePlayer<RampPlayer>>>();
    playback->Open(1000, {});
    playback->Start();
    RenderAhead ahead(std::move(playback));

    std::vector<int16_t> left;
    int16_t pcm[2 * 64];
    while (const std::size_t n = ahead.Read(pcm, 64))
    {
        for (std::size_t i = 0; i < n; i++)
        {
            left.push_back(pcm[2 * i]);
        }
    }

    Check(left.size() == 3000 && left[1000] == RampSample(1000), "every frame, rendered ahead at the set's rate");
    Check(ahead.Error().empty(), "no error at the set's rate");
}

} // namespace

int main()
{
    TestLengthAndFade();
    TestUntaggedEnd();
    TestSeekWithoutSnapshots();
    TestSeekWithSnapshots();
    TestRenderAhead<RampPlayer>("without snapshots");
    TestRenderAhead<SnapshotRampPlayer>("with snapshots");
    TestSetRateLength();
    TestSetRateUntaggedEnd();
    TestSetRateSnapshots();
    TestSetRateRenderAhead();

    return xsflib::testing::Summary("playback");
}
