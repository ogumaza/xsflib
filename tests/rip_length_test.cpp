// SPDX-License-Identifier: MIT

// Tests the length analysis (rip/length.h) on a scripted model: the timing helpers, loops, other tracks that hold the
// fade back, a loop with no sound after its first pass, a sequence that never sounds, the ends, the busy silence, the
// limit, a steady sequence, ringing effects, a failing frame, the rules that differ between models, and a model that
// follows only its output.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <new>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "xsflib/rip/length.h"
#include "xsflib/testing/check.h"

using namespace xsflib::testing;

namespace
{

namespace rip = xsflib::rip;

// A model whose frames a script sets up. The script runs at the start of each frame and may go round loops. The model
// has one voice. The script may play it.
struct ScriptedModel
{
    bool RunFrame()
    {
        if (fail_at && frames == *fail_at)
        {
            return false;
        }

        if (script)
        {
            script(*this, frames);
        }

        frames++;

        return true;
    }

    void Loop(int track, uint32_t target)
    {
        on_loop(track, target);
    }

    template <typename F>
    void OnLoop(F f)
    {
        on_loop = std::move(f);
    }

    bool Sounded() const
    {
        return sound;
    }

    bool Ringing() const
    {
        return ringing;
    }

    std::size_t VoiceCount() const
    {
        return voices.size();
    }

    rip::VoicePlace Voice(std::size_t i) const
    {
        return voices[i];
    }

    bool Active() const
    {
        return active;
    }

    bool Settled() const
    {
        return settled;
    }

    bool Stuck() const
    {
        return stuck;
    }

    bool Moving() const
    {
        return moving;
    }

    uint64_t ChangedAt() const
    {
        return changed_at;
    }

    bool Busy() const
    {
        return busy;
    }

    bool ChannelsChanging() const
    {
        return changing;
    }

    int ActiveChannels() const
    {
        return channels;
    }

    std::function<void(ScriptedModel&, uint64_t)> script;
    std::function<void(int, uint32_t)> on_loop;
    uint64_t frames = 0; // the frames run so far
    std::optional<uint64_t> fail_at;

    bool sound = false;
    bool ringing = false;
    bool active = true;
    bool settled = false;
    bool stuck = false;
    bool moving = false;
    bool busy = true;
    bool changing = false;
    uint64_t changed_at = 0;
    int channels = 1;
    std::vector<rip::VoicePlace> voices = {rip::VoicePlace{}};
};

// The rules of a model whose frame's commands sound in the same frame: offsets of 0, and an end at the last sound.
rip::LengthRules SameFrameRules()
{
    rip::LengthRules rules;
    rules.loop_offset = 0;
    rules.sound_offset = 0;
    rules.steady_offset = 0;
    rules.end_at_idle_start = false;

    return rules;
}

bool Is(const std::optional<rip::Timing>& timing, long long length_ms, long long fade_ms, bool silent = false)
{
    return timing && timing->length_ms == length_ms && timing->fade_ms == fade_ms && timing->silent == silent;
}

// The timing of the sequence that `script` plays with `rules`. With `frames`, also the frames the analysis ran.
std::optional<rip::Timing> Measure(const std::function<void(ScriptedModel&, uint64_t)>& script,
                                   const rip::LengthRules& rules = {}, uint64_t* frames = nullptr)
{
    ScriptedModel model;
    model.script = script;
    const std::optional<rip::Timing> timing = rip::MeasureLength(model, rules);

    if (frames)
    {
        *frames = model.frames;
    }

    return timing;
}

// 5 ms frames end with a tail of 500 ms. 3 ms frames loop and then fade over 10 s. The length is rounded to the nearest
// millisecond.
void TestTimings()
{
    const rip::Timing end = rip::EndAt(100, 5.0, 500.0);
    const rip::Timing loop = rip::LoopAt(333, 3.0, 10000.0);

    XSFLIB_CHECK(end.length_ms == 1000 && end.fade_ms == 0 && !end.silent);
    XSFLIB_CHECK(loop.length_ms == 999 && loop.fade_ms == 10000);
    XSFLIB_CHECK(rip::EndAt(1, 2.5, 0.0).length_ms == 3);
}

// Sequences that play together end with the latest of them and fade as the longest fade. A silent sequence doesn't
// count unless all are silent. A sequence without a timing leaves the piece without one.
void TestTimingOfPiece()
{
    const rip::Timing short_loop = {.length_ms = 1000, .fade_ms = 10000};
    const rip::Timing long_end = {.length_ms = 5000, .fade_ms = 0};
    const rip::Timing silent = {.length_ms = 9000, .fade_ms = 20000, .silent = true};

    const std::optional<rip::Timing> piece = rip::TimingOfPiece({silent, short_loop, long_end});
    XSFLIB_CHECK(piece && piece->length_ms == 5000 && piece->fade_ms == 10000 && !piece->silent);

    const rip::Timing short_silent = {.length_ms = 2000, .silent = true};
    const std::optional<rip::Timing> quiet = rip::TimingOfPiece({silent, short_silent});
    XSFLIB_CHECK(quiet && quiet->length_ms == 9000 && quiet->fade_ms == 20000 && quiet->silent);

    const std::optional<rip::Timing> alone = rip::TimingOfPiece({short_loop});
    XSFLIB_CHECK(alone && alone->length_ms == 1000 && alone->fade_ms == 10000);

    XSFLIB_CHECK(!rip::TimingOfPiece({short_loop, std::nullopt}));
    XSFLIB_CHECK(!rip::TimingOfPiece({}));
}

void TestLoops()
{
    // Track 0 goes round at frames 100 and 200. The loop's point is the end of frame 200 with the default offset, and
    // its start with the same-frame rules.
    const auto twice = [](ScriptedModel& m, uint64_t frame)
    {
        m.sound = true;

        if (frame == 100 || frame == 200)
        {
            m.Loop(0, 0x40);
        }
    };

    XSFLIB_CHECK(Is(Measure(twice), 603, 10000));
    XSFLIB_CHECK(Is(Measure(twice, SameFrameRules()), 600, 10000));

    // A loop of another track, or of another target, starts the count again.
    const auto others = [](ScriptedModel& m, uint64_t frame)
    {
        m.sound = true;

        if (frame == 100 || frame == 200 || frame == 300)
        {
            m.Loop(0, 0x40);
        }

        if (frame == 150)
        {
            m.Loop(1, 0x40);
        }

        if (frame == 250)
        {
            m.Loop(0, 0x80);
        }
    };
    ScriptedModel model;
    model.script = others;
    rip::LengthRules rules;
    rules.max_frames = 1000;

    XSFLIB_CHECK(Is(rip::MeasureLength(model, rules), 600000, 10000)); // no two passes of one loop in a row

    // Loops before the first sound don't count.
    const auto late = [](ScriptedModel& m, uint64_t frame)
    {
        m.sound = frame >= 50;

        if (frame == 10 || frame == 20 || frame == 60 || frame == 70)
        {
            m.Loop(0, 0x40);
        }
    };

    XSFLIB_CHECK(Is(Measure(late), 213, 10000));
}

// A scripted model whose other tracks loop at other lengths. They have looped twice once frame `others_at` has run.
struct OthersModel : ScriptedModel
{
    bool OthersLooped(int track) const
    {
        asked_for = track;

        return frames > others_at;
    }

    uint64_t others_at = 0;
    mutable int asked_for = -1; // the track whose loops count, as MeasureLength last gave it
};

void TestOthersLooped()
{
    // Track 1 goes round at frames 100 and 200. Other tracks that loop for the second time in frame 350 move the loop's
    // point to the end of frame 350, or its start with the same-frame rules. Others that looped before frame 200 leave
    // it at frame 200.
    const auto twice = [](ScriptedModel& m, uint64_t frame)
    {
        m.sound = true;

        if (frame == 100 || frame == 200)
        {
            m.Loop(1, 0x40);
        }
    };
    OthersModel late;
    late.script = twice;
    late.others_at = 350;
    OthersModel late_same_frame;
    late_same_frame.script = twice;
    late_same_frame.others_at = 350;
    OthersModel early;
    early.script = twice;
    early.others_at = 150;

    const std::optional<rip::Timing> late_timing = rip::MeasureLength(late, rip::LengthRules{});
    const std::optional<rip::Timing> late_same_frame_timing = rip::MeasureLength(late_same_frame, SameFrameRules());
    const std::optional<rip::Timing> early_timing = rip::MeasureLength(early, rip::LengthRules{});

    XSFLIB_CHECK(Is(late_timing, 1053, 10000) && late.asked_for == 1);
    XSFLIB_CHECK(Is(late_same_frame_timing, 1050, 10000));
    XSFLIB_CHECK(Is(early_timing, 603, 10000));
}

void TestLoopWithoutSound()
{
    // The sound stops after frame 100, the frame of the first pass. With the default rules, frame 100's commands sound
    // from frame 101 on. The sound then stopped before the pass. The sequence ends at its last sound. With the
    // same-frame rules, they sound in frame 100. The pass sounded. The sequence loops.
    const auto stops = [](ScriptedModel& m, uint64_t frame)
    {
        m.sound = frame <= 100;

        if (frame == 100 || frame == 200)
        {
            m.Loop(0, 0x40);
        }
    };

    XSFLIB_CHECK(Is(Measure(stops), 803, 0));
    XSFLIB_CHECK(Is(Measure(stops, SameFrameRules()), 600, 10000));
}

void TestEnds()
{
    // A sequence that never sounds is silent and gets the tail alone.
    const auto silent = [](ScriptedModel& m, uint64_t)
    {
        m.busy = false;
    };

    XSFLIB_CHECK(Is(Measure(silent), 500, 0, true));

    // Sound to frame 49, busy to frame 59. The idle frames run from 60 to 70. The sequence ends at its last sound.
    const auto ends = [](ScriptedModel& m, uint64_t frame)
    {
        m.sound = frame < 50;
        m.busy = frame < 60;
    };

    XSFLIB_CHECK(Is(Measure(ends), 650, 0));
    XSFLIB_CHECK(Is(Measure(ends, SameFrameRules()), 650, 0));

    // Sound goes on into the idle frames, to frame 65. With the default rules the sequence ends where they began. With
    // the same-frame rules it ends at the last sound.
    const auto sounds_on = [](ScriptedModel& m, uint64_t frame)
    {
        m.sound = frame <= 65;
        m.busy = frame < 60;
    };

    XSFLIB_CHECK(Is(Measure(sounds_on), 680, 0));
    XSFLIB_CHECK(Is(Measure(sounds_on, SameFrameRules()), 698, 0));

    // A stuck player with no channel left is idle while still busy.
    const auto stuck = [](ScriptedModel& m, uint64_t frame)
    {
        m.sound = frame < 30;
        m.stuck = frame >= 30;
        m.channels = frame >= 30 ? 0 : 1;
    };

    XSFLIB_CHECK(Is(Measure(stuck), 590, 0));
}

void TestBusySilenceAndLimit()
{
    // A sequence that sounds in frame 0 and stays busy without a sound ends after the silence's frames.
    rip::LengthRules rules;
    rules.silent_frames = 1000;
    ScriptedModel model;
    model.script = [](ScriptedModel& m, uint64_t frame)
    {
        m.sound = frame == 0;
    };

    XSFLIB_CHECK(Is(rip::MeasureLength(model, rules), 503, 0));
    XSFLIB_CHECK(model.frames == 1001);

    // At the limit, a sequence loops at the limit's length, whatever the frame's length.
    rules.max_frames = 500;
    rules.frame_ms = 160.0 * 1000.0 / 32728.0;
    model = {};
    model.script = [](ScriptedModel& m, uint64_t)
    {
        m.sound = true;
    };

    XSFLIB_CHECK(Is(rip::MeasureLength(model, rules), 600000, 10000));
    XSFLIB_CHECK(model.frames == 500);
}

void TestSteady()
{
    // One voice plays a looping wave from frame 0, and passes its loop start at frames 100 and 200. Nothing else
    // changes. The sequence loops at frame 200: at its end with the default offset, and at its start with the
    // same-frame rules.
    const auto steady = [](ScriptedModel& m, uint64_t frame)
    {
        m.sound = true;
        m.settled = true;
        m.voices = {{true, true, static_cast<int64_t>(frame % 100)}};
    };
    uint64_t frames = 0;

    XSFLIB_CHECK(Is(Measure(steady, {}, &frames), 603, 10000));
    XSFLIB_CHECK(frames == 201);
    XSFLIB_CHECK(Is(Measure(steady, SameFrameRules()), 600, 10000));

    // The same at a volume of 0 from frame 50. The sound changes in frame 50. The sequence ends at its last sound.
    const auto muted = [](ScriptedModel& m, uint64_t frame)
    {
        m.sound = frame < 50;
        m.settled = true;
        m.changed_at = frame >= 50 ? 1 : 0;
        m.voices = {{true, true, static_cast<int64_t>(frame % 100)}};
    };

    XSFLIB_CHECK(Is(Measure(muted, {}, &frames), 650, 0));
    XSFLIB_CHECK(frames == 201);

    // A wave that doesn't loop never makes the sequence steady.
    const auto one_shot = [](ScriptedModel& m, uint64_t frame)
    {
        m.sound = frame < 300;
        m.settled = true;
        m.busy = frame < 300;
        m.voices = {{frame < 300, false, static_cast<int64_t>(frame % 100)}};
    };

    XSFLIB_CHECK(Is(Measure(one_shot, {}, &frames), 1400, 0));
    XSFLIB_CHECK(frames == 311);
}

void TestRinging()
{
    // Effects ring on to frame 29 after the sequence ends at frame 9. The idle frames start at frame 30.
    rip::LengthRules rules;
    ScriptedModel model;
    model.script = [](ScriptedModel& m, uint64_t frame)
    {
        m.sound = frame < 10;
        m.busy = frame < 10;
        m.ringing = frame < 30;
    };

    XSFLIB_CHECK(Is(rip::MeasureLength(model, rules), 530, 0));
    XSFLIB_CHECK(model.frames == 41);
}

void TestFailures()
{
    ScriptedModel model;
    model.fail_at = 5;

    XSFLIB_CHECK(!rip::MeasureLength(model, rip::LengthRules{}));
    XSFLIB_CHECK(!rip::TryMeasure([]() -> std::optional<rip::Timing> { throw std::runtime_error("damaged bank"); }));

    // TryMeasure rethrows std::bad_alloc.
    const auto out_of_memory = []
    {
        rip::TryMeasure([]() -> std::optional<rip::Timing> { throw std::bad_alloc(); });
    };

    XSFLIB_CHECK(!ErrorOf<std::bad_alloc>(out_of_memory).empty());
}

// A model that follows its output alone. It has none of the tests from Ringing to ActiveChannels. A script sets each
// frame's sound and may go round loops.
struct OutputModel
{
    bool RunFrame()
    {
        script(*this, frames);
        frames++;

        return true;
    }

    void Loop(int track, uint32_t target)
    {
        on_loop(track, target);
    }

    template <typename F>
    void OnLoop(F f)
    {
        on_loop = std::move(f);
    }

    bool Sounded() const
    {
        return sound;
    }

    std::function<void(OutputModel&, uint64_t)> script;
    std::function<void(int, uint32_t)> on_loop;
    uint64_t frames = 0; // the frames run so far
    bool sound = false;
};

// An output model with each test written out, as the answer that MeasureLength takes for a missing test.
struct StubbedOutputModel : OutputModel
{
    bool Ringing() const
    {
        return false;
    }

    std::size_t VoiceCount() const
    {
        return 0;
    }

    rip::VoicePlace Voice(std::size_t) const
    {
        return {};
    }

    bool Active() const
    {
        return false;
    }

    bool Settled() const
    {
        return false;
    }

    bool Stuck() const
    {
        return false;
    }

    bool Moving() const
    {
        return false;
    }

    uint64_t ChangedAt() const
    {
        return 0;
    }

    bool Busy() const
    {
        return true;
    }

    bool ChannelsChanging() const
    {
        return false;
    }

    int ActiveChannels() const
    {
        return 0;
    }
};

// An output model with one test: its engine is busy while it sounds.
struct BusyOutputModel : OutputModel
{
    bool Busy() const
    {
        return sound;
    }
};

// The timing of the sequence that `script` plays on a `Model`, and the frames that the analysis ran.
template <typename Model>
std::pair<std::optional<rip::Timing>, uint64_t> MeasureOutput(const std::function<void(OutputModel&, uint64_t)>& script)
{
    rip::LengthRules rules;
    rules.max_frames = 5000;
    rules.max_ms = 15000.0;
    rules.silent_frames = 1000;
    Model model;
    model.script = script;
    const std::optional<rip::Timing> timing = rip::MeasureLength(model, rules);

    return {timing, model.frames};
}

// A model without the tests from Ringing to ActiveChannels measures as a model whose tests give the answers of an
// output model: its sequences end when they loop twice, when they fall silent for 1000 frames, or at the limit. A model
// that gives one of the tests uses it.
void TestOutputModel()
{
    const auto falls_silent = [](OutputModel& model, uint64_t frame)
    {
        model.sound = frame < 100;
    };

    const auto loops = [](OutputModel& model, uint64_t frame)
    {
        model.sound = true;
        if (frame % 200 == 199)
        {
            model.Loop(0, 4);
        }
    };

    const auto endless = [](OutputModel& model, uint64_t)
    {
        model.sound = true;
    };

    const auto silent = [](OutputModel&, uint64_t) {};

    const auto [silence, silence_frames] = MeasureOutput<OutputModel>(falls_silent);
    const auto [stubbed_silence, stubbed_silence_frames] = MeasureOutput<StubbedOutputModel>(falls_silent);
    const auto [looped, looped_frames] = MeasureOutput<OutputModel>(loops);
    const auto [stubbed_looped, stubbed_looped_frames] = MeasureOutput<StubbedOutputModel>(loops);
    const auto [limit, limit_frames] = MeasureOutput<OutputModel>(endless);
    const auto [stubbed_limit, stubbed_limit_frames] = MeasureOutput<StubbedOutputModel>(endless);
    const auto [never, never_frames] = MeasureOutput<OutputModel>(silent);
    const auto [stubbed_never, stubbed_never_frames] = MeasureOutput<StubbedOutputModel>(silent);
    const auto [idle, idle_frames] = MeasureOutput<BusyOutputModel>(falls_silent);

    XSFLIB_CHECK(Is(silence, 800, 0) && silence_frames == 1100);
    XSFLIB_CHECK(Is(stubbed_silence, 800, 0) && stubbed_silence_frames == 1100);
    XSFLIB_CHECK(Is(looped, 1200, 10000) && looped_frames == 400);
    XSFLIB_CHECK(Is(stubbed_looped, 1200, 10000) && stubbed_looped_frames == 400);
    XSFLIB_CHECK(Is(limit, 15000, 10000) && limit_frames == 5000);
    XSFLIB_CHECK(Is(stubbed_limit, 15000, 10000) && stubbed_limit_frames == 5000);
    XSFLIB_CHECK(Is(never, 500, 0, true) && never_frames == 1001);
    XSFLIB_CHECK(Is(stubbed_never, 500, 0, true) && stubbed_never_frames == 1001);

    // The engine falls idle with the sound. The sequence ends after 10 idle frames.
    XSFLIB_CHECK(Is(idle, 800, 0) && idle_frames == 111);
}

} // namespace

int main()
{
    const auto tests = []
    {
        TestTimings();
        TestTimingOfPiece();
        TestLoops();
        TestOthersLooped();
        TestLoopWithoutSound();
        TestEnds();
        TestBusySilenceAndLimit();
        TestSteady();
        TestRinging();
        TestFailures();
        TestOutputModel();
    };

    return xsflib::testing::RunTest("rip length", tests);
}
