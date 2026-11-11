// SPDX-License-Identifier: MIT

// A ripper's length analysis: how long a sequence plays, worked out by running it on a model of a game's sound code
// frame by frame. The caller passes an adapter for its model (see MeasureLength) and the rules of the analysis
// (LengthRules). The result is a Timing: the length and fade that a rip's tags give.

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <new>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace xsflib::rip
{

// A sequence's length and fade in milliseconds, and whether it never makes a sound.
struct Timing
{
    long long length_ms = 0;
    long long fade_ms = 0;
    bool silent = false;
};

// A sequence that ends: it plays `frame` frames, each `frame_ms` long, then `tail_ms` of silence. No fade follows.
Timing EndAt(uint64_t frame, double frame_ms, double tail_ms);

// A sequence that loops: it plays `frame` frames, each `frame_ms` long, then fades out over `fade_ms`.
Timing LoopAt(uint64_t frame, double frame_ms, double fade_ms);

// The timing of sequences that play together, from the timing that the length analysis gives each sequence alone: the
// latest end and the longest fade. Sequences that never make a sound don't count, unless all of them are silent.
// Without the timing of one of the sequences, there is none.
std::optional<Timing> TimingOfPiece(const std::vector<std::optional<Timing>>& timings);

// A looping sequence plays this many loops before its fade.
constexpr int kLengthLoops = 2;

// Sound is a sample louder than this (-72 dB).
constexpr int kSilenceLevel = 8;

// Whether any of `samples` is sound.
inline bool Audible(std::span<const int16_t> samples)
{
    return std::any_of(samples.begin(), samples.end(),
                       [](int16_t sample) { return sample > kSilenceLevel || sample < -kSilenceLevel; });
}

// The rules of the length analysis. The defaults give frames of 3 ms, two loops and a 10 s fade, a 0.5 s tail after the
// last sound, and at most 600 s.
struct LengthRules
{
    double frame_ms = 3.0;

    // The limit and the silence that ends a busy sequence, in frames. The caller rounds them. A model may count its
    // frames at another rate than its milliseconds.
    uint64_t max_frames = 200000;
    uint64_t silent_frames = 60000;

    double max_ms = 600000.0; // the limit's length
    double fade_ms = 10000.0; // a loop's fade
    double tail_ms = 500.0;   // the silence after the last sound of a sequence that ends

    // A sequence ends after more than this many frames in which it isn't busy, or can't change any more.
    uint64_t idle_frames = 10;

    // The frames that a loop and a steady sequence count from. Each is an offset from the frame that runs the commands.
    // A model whose frame's commands sound from the next frame on uses 1 for each. `loop_offset` gives the loop's
    // point. `sound_offset` gives the frame before which a loop's first pass must have sounded. `steady_offset` gives a
    // steady sequence's loop point.
    uint64_t loop_offset = 1;
    uint64_t sound_offset = 1;
    uint64_t steady_offset = 1;

    // Whether a sequence that ends ends at the first idle frame when that comes before its last sound's frame ends.
    // Otherwise it ends at its last sound.
    bool end_at_idle_start = true;
};

// A voice of the model's renderer: whether it plays, whether its wave loops, and how far it has played.
struct VoicePlace
{
    bool playing = false;
    bool loops = false;
    int64_t position = 0;
};

// Whether every track of `model` other than `track` has looped kLengthLoops times, by the model's OthersLooped (see
// MeasureLength). A model without it has no tracks that hold the fade back. `model` isn't const, so that a model's
// OthersLooped counts whether it's declared const or not.
template <typename Model>
bool OtherTracksLooped([[maybe_unused]] Model& model, [[maybe_unused]] int track)
{
    if constexpr (requires { model.OthersLooped(track); })
    {
        return model.OthersLooped(track);
    }
    else
    {
        return true;
    }
}

namespace detail
{

// The tests of a model's voices, sequence player and engine that MeasureLength runs. A test that the model doesn't
// provide gives the answer of a model that follows only its output (see MeasureLength). `Model&` isn't const, so that a
// model's tests count whether they're declared const or not.
template <typename Model>
class ModelTests
{
public:
    explicit ModelTests(Model& model) : model_(model)
    {
    }

    bool Ringing() const
    {
        if constexpr (requires { model_.Ringing(); })
        {
            return model_.Ringing();
        }
        else
        {
            return false;
        }
    }

    std::size_t VoiceCount() const
    {
        if constexpr (requires { model_.VoiceCount(); })
        {
            return model_.VoiceCount();
        }
        else
        {
            return 0;
        }
    }

    VoicePlace Voice([[maybe_unused]] std::size_t i) const
    {
        if constexpr (requires { model_.Voice(i); })
        {
            return model_.Voice(i);
        }
        else
        {
            return {};
        }
    }

    bool Active() const
    {
        if constexpr (requires { model_.Active(); })
        {
            return model_.Active();
        }
        else
        {
            return false;
        }
    }

    bool Settled() const
    {
        if constexpr (requires { model_.Settled(); })
        {
            return model_.Settled();
        }
        else
        {
            return false;
        }
    }

    bool Stuck() const
    {
        if constexpr (requires { model_.Stuck(); })
        {
            return model_.Stuck();
        }
        else
        {
            return false;
        }
    }

    bool Moving() const
    {
        if constexpr (requires { model_.Moving(); })
        {
            return model_.Moving();
        }
        else
        {
            return false;
        }
    }

    uint64_t ChangedAt() const
    {
        if constexpr (requires { model_.ChangedAt(); })
        {
            return model_.ChangedAt();
        }
        else
        {
            return 0;
        }
    }

    bool Busy() const
    {
        if constexpr (requires { model_.Busy(); })
        {
            return model_.Busy();
        }
        else
        {
            return true;
        }
    }

    bool ChannelsChanging() const
    {
        if constexpr (requires { model_.ChannelsChanging(); })
        {
            return model_.ChannelsChanging();
        }
        else
        {
            return false;
        }
    }

    int ActiveChannels() const
    {
        if constexpr (requires { model_.ActiveChannels(); })
        {
            return model_.ActiveChannels();
        }
        else
        {
            return 0;
        }
    }

private:
    Model& model_;
};

// MeasureLength's state as it plays a sequence on a model, and the steps of each frame.
template <typename Model>
class LengthMeasure
{
public:
    LengthMeasure(Model& model, const LengthRules& rules) : model_(model), tests_(model), rules_(rules)
    {
    }

    LengthMeasure(const LengthMeasure&) = delete;
    LengthMeasure& operator=(const LengthMeasure&) = delete;

    std::optional<Timing> Run()
    {
        model_.OnLoop([this](int track, uint32_t target) { CountLoop(track, target); });
        voices_ = tests_.VoiceCount();
        places_.assign(voices_, {});
        passes_.assign(voices_, {});
        for (; frame_ < rules_.max_frames; ++frame_)
        {
            if (!model_.RunFrame())
            {
                return std::nullopt;
            }

            if (model_.Sounded())
            {
                sounded_ = true;
                last_sound_ = frame_;
            }

            if (const std::optional<Timing> looped = LoopTiming())
            {
                return Heard(*looped);
            }

            if (const std::optional<Timing> steady = SteadyTiming())
            {
                return Heard(*steady);
            }

            const bool busy = tests_.Busy();
            if (FellSilent(busy) || StoppedChanging(busy))
            {
                break;
            }
        }

        return Heard(EndTiming());
    }

private:
    Timing EndAt(uint64_t frame) const
    {
        return rip::EndAt(frame, rules_.frame_ms, rules_.tail_ms);
    }

    Timing LoopAt(uint64_t frame) const
    {
        return rip::LoopAt(frame, rules_.frame_ms, rules_.fade_ms);
    }

    // Whether it ends or loops, a sequence that never sounds is silent and gets the tail alone. Such a sequence may
    // still play notes that make no sound.
    Timing Heard(Timing timing) const
    {
        if (!sounded_)
        {
            timing = EndAt(0);
            timing.silent = true;
        }

        return timing;
    }

    // Loops that go round before the first sound don't count. An example is a track that waits or sets values while a
    // note fades in or another track starts. A loop of another track, or one that starts elsewhere, starts the count
    // again. The loops counted so far don't repeat what plays now. That happens when track 0 polls a variable while a
    // later track plays the music, or when a track moves on to the loop it plays for good.
    void CountLoop(int track, uint32_t target)
    {
        if (!sounded_)
        {
            return;
        }

        if (std::pair{track, target} != loop_)
        {
            loops_seen_ = 0;
            loop_ = {track, target};
        }

        ++loops_seen_;
        if (loops_seen_ == 1)
        {
            first_loop_frame_ = frame_ + rules_.sound_offset;
        }

        if (loops_seen_ == kLengthLoops)
        {
            loop_frame_ = frame_ + rules_.loop_offset;
        }
    }

    // A loop that went round without a sound, such as a wait that a track repeats once its notes have ended, isn't
    // music: the sequence ended at its last sound. Other tracks that loop at other lengths hold the fade back until
    // each of them has looped kLengthLoops times too (the model's OthersLooped). Until then, each frame moves the
    // loop's point on to the next frame. The fade then starts where the last of them looped.
    std::optional<Timing> LoopTiming()
    {
        if (loops_seen_ < kLengthLoops)
        {
            return std::nullopt;
        }

        if (OtherTracksLooped(model_, loop_.first))
        {
            return last_sound_ < first_loop_frame_ ? EndAt(last_sound_ + 1) : LoopAt(loop_frame_);
        }

        loop_frame_ = frame_ + 1 + rules_.loop_offset;

        return std::nullopt;
    }

    // Some sequences reach a point where every open track waits. A track may wait for notes that never end (held notes
    // on looping waves), or never read on again, or not before the game stops the sound. A track may also poll a
    // variable once a tick and change nothing. Such a sequence is steady once nothing else changes. No note's envelope
    // is in its attack, hold, decay or release. No volume, pan or pitch bend is moving. (A released note ends by
    // itself. That can let its track read on.) A steady sequence plays on unchanged but for the waves' positions. So it
    // loops when they do. Each running voice's passes back to its wave's loop start are followed throughout. A steady
    // sequence has looped when every voice has passed kLengthLoops times since the sound last changed. A track's
    // change, a moving envelope or ramp, and a voice that starts or stops all change the sound. A track that polls may
    // only show that it's still after a long pass. The waves' passes during that pass count.
    std::optional<Timing> SteadyTiming()
    {
        FollowSound();
        const bool steady = tests_.Active() && tests_.Settled() && changed_ < frame_;
        if (!steady || !WavesLooped())
        {
            return std::nullopt;
        }

        // A sequence that has made no sound since its sound last changed, such as a held note at a volume of 0, ended
        // at its last sound.
        return last_sound_ < changed_ ? EndAt(last_sound_ + 1) : LoopAt(frame_ + rules_.steady_offset);
    }

    void FollowSound()
    {
        for (std::size_t i = 0; i < voices_; ++i)
        {
            const VoicePlace place = tests_.Voice(i);
            if (place.playing != places_[i].playing)
            {
                changed_ = frame_;
            }
            else if (place.playing && place.position < places_[i].position)
            {
                std::array<uint64_t, kLengthLoops>& latest = passes_[i];
                std::copy_backward(latest.begin(), latest.end() - 1, latest.end());
                latest[0] = frame_;
            }

            places_[i] = place;
        }

        if (tests_.ChannelsChanging() || tests_.Moving() || tests_.ChangedAt() != change_order_)
        {
            changed_ = frame_;
        }

        change_order_ = tests_.ChangedAt();
    }

    bool WavesLooped() const
    {
        bool any = false;
        for (std::size_t i = 0; i < voices_; ++i)
        {
            if (!places_[i].playing)
            {
                continue;
            }

            if (!places_[i].loops || passes_[i].back() <= changed_)
            {
                return false;
            }

            any = true;
        }

        return any;
    }

    // A sequence that stays busy without a sound for the silence's length, such as one whose music has ended while a
    // track that plays no notes runs on, ended where it fell silent.
    bool FellSilent(bool busy)
    {
        if (!busy || frame_ - last_sound_ < rules_.silent_frames)
        {
            return false;
        }

        idle_ = frame_ - last_sound_;

        return true;
    }

    // A sequence that will never change again, or not before the game stops it, has ended when its last note has. Its
    // tracks may wait for ever or poll a variable that only the game sets. Effects that still ring hold the end back.
    bool StoppedChanging(bool busy)
    {
        const bool stuck = tests_.Active() && tests_.Stuck() && tests_.ActiveChannels() == 0;
        if ((!busy || stuck) && !tests_.Ringing())
        {
            return ++idle_ > rules_.idle_frames;
        }

        idle_ = 0;

        return false;
    }

    // Ended: the length runs to the end of the last frame with sound and the tail, with no fade. Only silence follows,
    // however long the sequence takes to end (a track may hold a note at a volume of 0 for minutes). The idle frames
    // began at `frame_ + 1 - idle_`. A sequence that plays to the limit gets the limit as its length, and the fade.
    Timing EndTiming() const
    {
        if (frame_ >= rules_.max_frames)
        {
            return Timing{static_cast<long long>(rules_.max_ms), static_cast<long long>(rules_.fade_ms)};
        }

        const uint64_t end = rules_.end_at_idle_start ? std::min(frame_ + 1 - idle_, last_sound_ + 1) : last_sound_ + 1;

        return EndAt(end);
    }

    Model& model_;
    const ModelTests<Model> tests_;
    const LengthRules& rules_;

    uint64_t frame_ = 0;
    bool sounded_ = false;
    uint64_t last_sound_ = 0;
    uint64_t idle_ = 0;

    // The loop that counts (a track and its target), its passes so far, and the frames that they give.
    int loops_seen_ = 0;
    std::pair<int, uint32_t> loop_{-1, 0};
    uint64_t first_loop_frame_ = 0;
    uint64_t loop_frame_ = 0;

    // The renderer's voices.
    std::size_t voices_ = 0;
    std::vector<VoicePlace> places_;

    // The frames of each voice's latest passes, the newest first.
    std::vector<std::array<uint64_t, kLengthLoops>> passes_;
    uint64_t changed_ = 0;      // the frame of the sound's last change
    uint64_t change_order_ = 0; // ChangedAt in that frame
};

} // namespace detail

// Plays a sequence on a model and works out its timing. Returns nothing if a frame fails. The model's adapter, `model`,
// has the sequence started, and provides these:
//
// - `bool RunFrame()` runs a frame of the engine and the renderer. It returns false if the model fails.
// - `bool Sounded()`: whether the frame's output had sound (Audible).
// - `bool Ringing()`: whether effects still ring in the output. A sequence doesn't end while they do.
// - `std::size_t VoiceCount()` and `VoicePlace Voice(std::size_t)`: the renderer's voices.
// - `bool Active()`, `Settled()`, `Stuck()` and `Moving()`, and `uint64_t ChangedAt()`: the sequence player's tests.
// - `bool Busy()`, `bool ChannelsChanging()` and `int ActiveChannels()`: the engine's tests.
// - `void OnLoop(F)` sets the player's loop hook to `F`, a callable that takes a track and the loop's target.
//
// The model may also provide `bool OthersLooped(int track)`: whether every track other than `track`, the track whose
// loops count, has looped kLengthLoops times or doesn't hold the fade back. A model whose tracks loop at other lengths
// then gets its fade once each of them has played its loop twice.
//
// A model may leave out any of the tests from `Ringing` to `ActiveChannels`. MeasureLength then takes the answer of a
// model that follows only its output: no voices, a sequence player that's never active, settled, stuck or moving, and
// an engine that's always busy, with no channel changing or active and no effect ringing. A sequence on a model without
// any of these tests ends when it loops, falls silent for good or reaches the limit.
//
// MeasureLength plays until the sequence loops kLengthLoops times, ends, falls silent for good, or reaches the limit.
template <typename Model>
std::optional<Timing> MeasureLength(Model& model, const LengthRules& rules)
{
    detail::LengthMeasure<Model> measure(model, rules);
    return measure.Run();
}

// Runs `measure` and returns its timing. `measure` plays a sequence on a model. A sequence the model can't play, with a
// damaged bank for instance, gets no timing. Running out of memory isn't the sequence's fault. std::bad_alloc goes to
// the caller instead.
template <typename Measure>
std::optional<Timing> TryMeasure(Measure measure)
{
    try
    {
        return measure();
    }
    catch (const std::bad_alloc&)
    {
        throw;
    }
    catch (const std::exception&)
    {
        return std::nullopt;
    }
}

} // namespace xsflib::rip
