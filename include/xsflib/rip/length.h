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
    long long length_ms;
    long long fade_ms;
    bool silent = false;
};

// A sequence that ends: it plays `frame` frames, each `frame_ms` long, then `tail_ms` of silence. No fade follows.
Timing EndAt(uint64_t frame, double frame_ms, double tail_ms);

// A sequence that loops: it plays `frame` frames, each `frame_ms` long, then fades out over `fade_ms`.
Timing LoopAt(uint64_t frame, double frame_ms, double fade_ms);

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
// MeasureLength plays until the sequence loops kLengthLoops times, ends, falls silent for good, or reaches the limit.
template <typename Model>
std::optional<Timing> MeasureLength(Model& model, const LengthRules& rules)
{
    const auto end_at = [&rules](uint64_t frame)
    {
        return EndAt(frame, rules.frame_ms, rules.tail_ms);
    };

    const auto loop_at = [&rules](uint64_t frame)
    {
        return LoopAt(frame, rules.frame_ms, rules.fade_ms);
    };

    // Whether it ends or loops, a sequence that never sounds is silent and gets the tail alone. Such a sequence may
    // still play notes that make no sound.
    bool sounded = false;
    const auto heard = [&sounded, &end_at](Timing timing)
    {
        if (!sounded)
        {
            timing = end_at(0);
            timing.silent = true;
        }

        return timing;
    };

    // Loops that go round before the first sound don't count. An example is a track that waits or sets values while a
    // note fades in or another track starts. A loop of another track, or one that starts elsewhere, starts the count
    // again. The loops counted so far don't repeat what plays now. That happens when track 0 polls a variable while a
    // later track plays the music, or when a track moves on to the loop it plays for good.
    int loops_seen = 0;
    std::pair<int, uint32_t> loop{-1, 0};
    uint64_t frame = 0;
    uint64_t first_loop_frame = 0;
    uint64_t loop_frame = 0;
    const auto count_loop = [&](int track, uint32_t target)
    {
        if (!sounded)
        {
            return;
        }

        if (std::pair{track, target} != loop)
        {
            loops_seen = 0;
            loop = {track, target};
        }

        ++loops_seen;
        if (loops_seen == 1)
        {
            first_loop_frame = frame + rules.sound_offset;
        }
        if (loops_seen == kLengthLoops)
        {
            loop_frame = frame + rules.loop_offset;
        }
    };
    model.OnLoop(count_loop);

    // Some sequences reach a point where every open track waits. A track may wait for notes that never end (held notes
    // on looping waves), or never read on again, or not before the game stops the sound. A track may also poll a
    // variable once a tick and change nothing. Such a sequence is steady once nothing else changes. No note's envelope
    // is in its attack, hold, decay or release. No volume, pan or pitch bend is moving. (A released note ends by
    // itself. That can let its track read on.) A steady sequence plays on unchanged but for the waves' positions. So it
    // loops when they do. Each running voice's passes back to its wave's loop start are followed throughout. A steady
    // sequence has looped when every voice has passed kLengthLoops times since the sound last changed. A track's
    // change, a moving envelope or ramp, and a voice that starts or stops all change the sound. A track that polls may
    // only show that it's still after a long pass. The waves' passes during that pass count.
    const std::size_t voices = model.VoiceCount();
    std::vector<VoicePlace> places(voices);
    // The frames of each voice's latest passes, the newest first.
    std::vector<std::array<uint64_t, kLengthLoops>> passes(voices);
    uint64_t changed = 0;      // the frame of the sound's last change
    uint64_t change_order = 0; // ChangedAt in that frame
    const auto follow_sound = [&]
    {
        for (std::size_t i = 0; i < voices; ++i)
        {
            const VoicePlace place = model.Voice(i);
            if (place.playing != places[i].playing)
            {
                changed = frame;
            }
            else if (place.playing && place.position < places[i].position)
            {
                std::array<uint64_t, kLengthLoops>& latest = passes[i];
                std::copy_backward(latest.begin(), latest.end() - 1, latest.end());
                latest[0] = frame;
            }
            places[i] = place;
        }

        if (model.ChannelsChanging() || model.Moving() || model.ChangedAt() != change_order)
        {
            changed = frame;
        }
        change_order = model.ChangedAt();
    };

    const auto waves_looped = [&]
    {
        bool any = false;
        for (std::size_t i = 0; i < voices; ++i)
        {
            if (!places[i].playing)
            {
                continue;
            }

            if (!places[i].loops || passes[i].back() <= changed)
            {
                return false;
            }

            any = true;
        }

        return any;
    };

    uint64_t idle = 0;
    uint64_t last_sound = 0;
    for (; frame < rules.max_frames; ++frame)
    {
        if (!model.RunFrame())
        {
            return std::nullopt;
        }

        if (model.Sounded())
        {
            sounded = true;
            last_sound = frame;
        }

        // A loop that went round without a sound, such as a wait that a track repeats once its notes have ended, isn't
        // music: the sequence ended at its last sound.
        if (loops_seen >= kLengthLoops)
        {
            return heard(last_sound < first_loop_frame ? end_at(last_sound + 1) : loop_at(loop_frame));
        }

        follow_sound();
        const bool steady = model.Active() && model.Settled() && changed < frame;
        if (steady && waves_looped())
        {
            // A sequence that has made no sound since its sound last changed, such as a held note at a volume of 0,
            // ended at its last sound.
            return heard(last_sound < changed ? end_at(last_sound + 1) : loop_at(frame + rules.steady_offset));
        }

        // A sequence that stays busy without a sound for the silence's length, such as one whose music has ended while
        // a track that plays no notes runs on, ended where it fell silent.
        const bool busy = model.Busy();
        if (busy && frame - last_sound >= rules.silent_frames)
        {
            idle = frame - last_sound;
            break;
        }

        // A sequence that will never change again, or not before the game stops it, has ended when its last note has.
        // Its tracks may wait for ever or poll a variable that only the game sets. Effects that still ring hold the end
        // back.
        const bool stuck = model.Active() && model.Stuck() && model.ActiveChannels() == 0;
        if ((!busy || stuck) && !model.Ringing())
        {
            if (++idle > rules.idle_frames)
            {
                break;
            }
        }
        else
        {
            idle = 0;
        }
    }
    if (frame >= rules.max_frames)
    {
        return heard(Timing{static_cast<long long>(rules.max_ms), static_cast<long long>(rules.fade_ms)});
    }

    // Ended: the length runs to the end of the last frame with sound and the tail, with no fade. Only silence follows,
    // however long the sequence takes to end (a track may hold a note at a volume of 0 for minutes). The idle frames
    // began at `frame + 1 - idle`.
    const uint64_t end = rules.end_at_idle_start ? std::min(frame + 1 - idle, last_sound + 1) : last_sound + 1;

    return heard(end_at(end));
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
