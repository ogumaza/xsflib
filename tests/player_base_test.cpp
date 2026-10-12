// SPDX-License-Identifier: MIT

// Unit tests for the framework of a format's Player (xsflib/player/player.h): loading, starting and stepping with their
// failures, rendering and the end of the sound, the boot of a driver with a status word, the tags' length, fade and
// volume, and snapshots.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "xsflib/player/player.h"
#include "xsflib/psf/psf.h"
#include "xsflib/testing/check.h"

using xsflib::testing::failures;

namespace
{

// A set: its tags, how many steps the sound lasts, how its start and steps go, and the status words that its driver
// reports while the game boots.
struct TestSet
{
    xsflib::psf::Tags tags;
    bool playable = true;
    int steps = 0;
    bool throw_on_start = false;
    int throw_at_step = -1;
    std::vector<uint32_t> boot_status;
};

// The source's part of a snapshot.
struct TestSource
{
    int next = 0;
    int steps = 0;
};

// A player whose every step adds 3 frames, counting up from 1, and whose sound finishes after the set's steps. With a
// boot status, it first runs a driver's boot, a step for each status word.
class TestPlayer : public xsflib::player::PlayerBase<TestPlayer, TestSet>
{
public:
    using Snapshot = xsflib::player::Snapshot<TestSource>;

    int start_failures = 0;
    int unloads = 0;

private:
    friend class xsflib::player::PlayerBase<TestPlayer, TestSet>;

    static constexpr const char* kNotPlayable = "nothing to play";
    static constexpr const char* kStartFailure = "the test failed: ";
    static constexpr const char* kStepFailure = "the step failed: ";

    static std::optional<std::string> LoadFile(const std::string& path, const xsflib::psf::FileReader& reader, Set& set)
    {
        std::vector<uint8_t> data;
        if (!reader(path, data))
        {
            return "can't read " + path;
        }

        set.steps = static_cast<int>(data.size());
        set.tags["length"] = "1.5";

        return std::nullopt;
    }

    void Unload()
    {
        unloads++;
        started_ = false;
    }

    bool Playable() const
    {
        return set_.playable;
    }

    bool StartSource()
    {
        if (set_.throw_on_start)
        {
            throw std::runtime_error("no sound");
        }

        output_.clear();
        steps_ = 0;
        next_ = 0;
        status_ = 0;
        started_ = true;

        if (!set_.boot_status.empty())
        {
            booting_ = true;
            const bool booted = RunUntilSoundPlays(100);
            booting_ = false;
            steps_ = 0;
            return booted;
        }

        return true;
    }

    bool RunStep()
    {
        if (steps_ == set_.throw_at_step)
        {
            throw std::runtime_error("bad step");
        }

        for (int i = 0; i < 3; i++)
        {
            next_++;
            output_.push_back(static_cast<int16_t>(next_));
            output_.push_back(static_cast<int16_t>(-next_));
        }

        steps_++;
        if (booting_)
        {
            status_++;
        }

        return true;
    }

    bool Finished() const
    {
        return steps_ >= set_.steps;
    }

    std::vector<int16_t>& Output()
    {
        return output_;
    }

    void OnStartFailure()
    {
        start_failures++;
    }

    uint32_t DriverStatus() const
    {
        return status_ < set_.boot_status.size() ? set_.boot_status[status_] : 1;
    }

    uint64_t EmulatedCycles() const
    {
        return static_cast<uint64_t>(next_) * 10;
    }

    std::shared_ptr<const TestSource> SaveSource(const TestSource*)
    {
        if (!started_)
        {
            return nullptr;
        }

        return std::make_shared<TestSource>(TestSource{next_, steps_});
    }

    bool CanRestore(const TestSource* source) const
    {
        return started_ && source;
    }

    void RestoreSource(const TestSource& source)
    {
        next_ = source.next;
        steps_ = source.steps;
    }

    std::vector<int16_t> output_;
    bool started_ = false;
    bool booting_ = false;
    std::size_t status_ = 0;
    int steps_ = 0;
    int next_ = 0;
};

TestSet Steps(int steps)
{
    TestSet set;
    set.steps = steps;

    return set;
}

void TestLoadAndRender()
{
    const xsflib::psf::FileReader reader = [](const std::string& path, std::vector<uint8_t>& data)
    {
        if (path != "song")
        {
            return false;
        }

        data.assign(4, 0);

        return true;
    };

    TestPlayer player;

    XSFLIB_CHECK(!player.Load("missing", reader));
    XSFLIB_CHECK(player.GetState() == TestPlayer::State::kError && player.Error() == "can't read missing");

    XSFLIB_CHECK(player.Load("song", reader));
    XSFLIB_CHECK(player.unloads == 1 && player.GetState() == TestPlayer::State::kIdle && player.Error().empty());
    XSFLIB_CHECK(player.LengthMs() == 1500 && player.FadeMs() == -1 && player.Volume() == 1.0);
    XSFLIB_CHECK(player.GetLoadedSet().steps == 4 && player.GetTags().at("length") == "1.5");

    // Rendering before the start gives silence.
    int16_t out[16];
    std::fill(out, out + 16, int16_t{7});

    XSFLIB_CHECK(player.Render(out, 8) == 0 && out[0] == 0 && out[15] == 0);

    XSFLIB_CHECK(player.Start());
    XSFLIB_CHECK(player.Render(out, 5) == 5);
    XSFLIB_CHECK(out[0] == 1 && out[1] == -1 && out[8] == 5 && out[9] == -5);
    XSFLIB_CHECK(player.GetState() == TestPlayer::State::kPlaying);

    // Four steps of 3 frames finish the sound at frame 12, whatever block size Render is called with.
    XSFLIB_CHECK(player.Render(out, 8) == 8);
    XSFLIB_CHECK(out[0] == 6 && out[14] == 13);
    XSFLIB_CHECK(player.GetState() == TestPlayer::State::kFinished && player.FinishedFrame() == 12);

    // A restart plays from the beginning.
    XSFLIB_CHECK(player.Start() && player.GetState() == TestPlayer::State::kPlaying);
    XSFLIB_CHECK(player.Render(out, 2) == 2 && out[0] == 1 && out[2] == 2);
}

void TestFailures()
{
    // A set with nothing to play.
    TestPlayer player;
    TestSet set = Steps(4);
    set.playable = false;

    XSFLIB_CHECK(!player.Load(set));
    XSFLIB_CHECK(player.GetState() == TestPlayer::State::kError && player.Error() == "nothing to play");
    XSFLIB_CHECK(!player.Start() && player.Error() == "nothing loaded");

    // A start that throws.
    set = Steps(4);
    set.throw_on_start = true;

    XSFLIB_CHECK(player.Load(set));
    XSFLIB_CHECK(!player.Start());
    XSFLIB_CHECK(player.GetState() == TestPlayer::State::kError && player.Error() == "the test failed: no sound");
    XSFLIB_CHECK(player.start_failures == 1);

    // A step that throws stops the render with silence.
    set = Steps(10);
    set.throw_at_step = 2;

    XSFLIB_CHECK(player.Load(set) && player.Start());
    int16_t out[16];
    XSFLIB_CHECK(player.Render(out, 8) == 0 && out[0] == 0);
    XSFLIB_CHECK(player.GetState() == TestPlayer::State::kError && player.Error() == "the step failed: bad step");
}

void TestBoot()
{
    // The driver boots for three steps, then plays. The samples of the boot are dropped.
    TestPlayer player;
    TestSet set = Steps(4);
    set.boot_status = {0, 0, 0};

    XSFLIB_CHECK(player.Load(set) && player.Start());
    int16_t out[4];
    XSFLIB_CHECK(player.Render(out, 2) == 2 && out[0] == 10 && out[2] == 11);

    // A driver error, with the step in the low byte.
    set.boot_status = {0, 0x80000007u};

    XSFLIB_CHECK(player.Load(set) && !player.Start());
    XSFLIB_CHECK(player.Error() == "driver error at step 7");

    // A driver that never starts the sound.
    set.boot_status.assign(50, 0);

    XSFLIB_CHECK(player.Load(set) && !player.Start());
    XSFLIB_CHECK(player.Error() == "timed out waiting for the sound to start");
}

void TestSnapshots()
{
    TestPlayer player;

    XSFLIB_CHECK(player.Load(Steps(6)));
    XSFLIB_CHECK(!player.Save());
    XSFLIB_CHECK(player.Start());

    // A snapshot after 4 frames, with 2 frames of the second step waiting.
    int16_t out[8];
    XSFLIB_CHECK(player.Render(out, 4) == 4);

    const std::shared_ptr<const TestPlayer::Snapshot> snapshot = player.Save();

    XSFLIB_CHECK(snapshot && snapshot->pending.size() == 4 && snapshot->dropped == 8);

    int16_t first[16];
    XSFLIB_CHECK(player.Render(first, 8) == 8);

    XSFLIB_CHECK(player.Restore(*snapshot));
    int16_t again[16];
    XSFLIB_CHECK(player.Render(again, 8) == 8);
    XSFLIB_CHECK(std::equal(first, first + 16, again) && again[0] == 5);

    // The sound finished at frame 18 in both renders.
    XSFLIB_CHECK(player.Render(out, 4) == 4);
    XSFLIB_CHECK(player.GetState() == TestPlayer::State::kFinished && player.FinishedFrame() == 18);

    // Another player, or a later run, can't restore it.
    TestPlayer other;
    XSFLIB_CHECK(other.Load(Steps(6)) && other.Start());

    XSFLIB_CHECK(!other.Restore(*snapshot) && other.GetState() == TestPlayer::State::kPlaying);

    XSFLIB_CHECK(player.Start());
    XSFLIB_CHECK(!player.Restore(*snapshot));

    // A snapshot of another run or player isn't used to share with.
    const auto later = player.Save(snapshot.get());
    XSFLIB_CHECK(later && later->run == 2);
}

} // namespace

int main()
{
    TestLoadAndRender();
    TestFailures();
    TestBoot();
    TestSnapshots();

    if (failures)
    {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }

    std::printf("player base tests passed\n");

    return 0;
}
