// SPDX-License-Identifier: MIT

// The framework of a format's Player, PlayerBase. It holds what every player has: the set, the state, the error and the
// counts of samples. PlayerBase loads a set, starts the sound and runs it a step at a time with the failures caught,
// renders from the samples that the sound's source produces, detects the end of the sound, and takes and restores
// snapshots. A format's Player derives from PlayerBase<Player, Set> and makes it a friend. The Player's source is the
// console's emulation or model that plays the sound. The Player provides:
//
//   static LoadFile         (path, reader, set) -> std::optional<std::string>, the loader's error
//   kNotPlayable            the error for a set with nothing to play ("no process or archive descriptor")
//   kStartFailure           the start of the message for a start that threw ("emulation failed: ")
//   kStepFailure            the start of the message for a step that threw
//   Unload()                releases the source of the previous set. Load calls it before it takes a new set.
//   Playable() const        whether the set has something the player can play
//   StartSource()           starts the sound's source, and returns false after calling Fail. It may throw.
//   RunStep()               runs the source one step. The step adds samples to Output(). Returns false after calling
//                           Fail. It may throw.
//   Finished()              whether the sound has finished. Render checks it after each step.
//   Output()                the samples produced and not dropped yet, two per frame
//
// and, if it requires them:
//
//   OnStartFailure()        clears what a start that threw left behind
//   DriverStatus() and      the status word of a game-mode driver and the emulated cycles since the boot, for
//   EmulatedCycles()        RunUntilSoundPlays
//
// A player with snapshots declares `using Snapshot = xsflib::player::Snapshot<SourceSnapshot>;`. SourceSnapshot is the
// source's part of a snapshot. The player also provides:
//
//   SaveSource(previous)    -> std::shared_ptr<const SourceSnapshot>, the source's part, sharing what it can with
//                           `previous` (or nullptr). Returns nullptr when there's nothing to save. It may throw.
//   CanRestore(source)      whether the part `source` points at fits the current source
//   RestoreSource(source)   restores the source's part. It may throw.
//
// The helpers in modes.h implement these hooks for a player with an archive mode and a game mode.

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "xsflib/psf/psf.h"

namespace xsflib::player
{

// What a player is doing. A loaded player is idle until it starts.
enum class State
{
    kIdle,
    kPlaying,
    kFinished, // the sound has finished, and the player renders what follows it
    kError,
};

// A snapshot of a player between steps: the player's state, the samples produced but not handed out yet, and the
// source's part. Only the player that took it can restore it, and only before that player's next Start.
template <typename SourceSnapshot>
struct Snapshot
{
    const void* owner = nullptr;
    uint64_t run = 0;
    State state = State::kIdle;
    uint64_t finished_frame = 0;
    uint64_t dropped = 0;         // samples produced before `pending`
    std::vector<int16_t> pending; // samples produced but not handed out yet
    std::shared_ptr<const SourceSnapshot> source;
};

template <typename Derived, typename SetType>
class PlayerBase
{
public:
    using State = player::State;
    using Set = SetType;

    // Loads a file and its libraries, without starting.
    bool Load(const std::string& path, const psf::FileReader& reader)
    {
        Set set;
        if (auto err = Derived::LoadFile(path, reader, set))
        {
            return Fail(*err);
        }

        return Load(std::move(set));
    }

    // Takes an assembled set, without starting.
    bool Load(Set set)
    {
        Self().Unload();
        set_ = std::move(set);
        state_ = State::kIdle;
        error_.clear();
        if (!Self().Playable())
        {
            return Fail(Derived::kNotPlayable);
        }

        return true;
    }

    // Starts the sound. Can be called again to restart from the beginning.
    bool Start()
    {
        // The source's failures arrive as exceptions.
        try
        {
            run_++;
            consumed_ = 0;
            dropped_ = 0;
            finished_frame_ = 0;
            error_.clear();

            if (!Self().Playable())
            {
                return Fail("nothing loaded");
            }

            if (!Self().StartSource())
            {
                return false;
            }

            state_ = State::kPlaying;
            return true;
        }
        catch (const std::exception& e)
        {
            if constexpr (requires(Derived& d) { d.OnStartFailure(); })
            {
                Self().OnStartFailure();
            }

            return Fail(std::string(Derived::kStartFailure) + e.what());
        }
    }

    // Renders up to `frames` stereo frames (interleaved L, R) into `out`. Returns the number of frames written. The
    // count is less than `frames` only after an error.
    std::size_t Render(int16_t* out, std::size_t frames)
    {
        if (state_ != State::kPlaying && state_ != State::kFinished)
        {
            std::fill(out, out + frames * 2, int16_t{0});
            return 0;
        }

        auto& buf = Self().Output();
        while ((buf.size() - consumed_) / 2 < frames)
        {
            if (!Step())
            {
                std::fill(out, out + frames * 2, int16_t{0});
                return 0;
            }

            if (state_ == State::kPlaying && Self().Finished())
            {
                state_ = State::kFinished;
                finished_frame_ = (dropped_ + buf.size()) / 2;
            }
        }

        std::copy(buf.begin() + static_cast<std::ptrdiff_t>(consumed_),
                  buf.begin() + static_cast<std::ptrdiff_t>(consumed_ + frames * 2), out);
        consumed_ += frames * 2;

        // Drop consumed samples now and then to bound memory.
        if (consumed_ > (1u << 20))
        {
            buf.erase(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(consumed_));
            dropped_ += consumed_;
            consumed_ = 0;
        }

        return frames;
    }

    State GetState() const
    {
        return state_;
    }

    // The frame count at the end of the step that first found the sound finished, whatever block size Render was called
    // with. Only meaningful in State::kFinished.
    uint64_t FinishedFrame() const
    {
        return finished_frame_;
    }

    const std::string& Error() const
    {
        return error_;
    }

    // The tags of the set's top-level file.
    const psf::Tags& GetTags() const
    {
        return set_.tags;
    }

    // The set as the player loaded it.
    const Set& GetLoadedSet() const
    {
        return set_;
    }

    // The tagged length and fade in milliseconds, or -1 if untagged.
    long long LengthMs() const
    {
        return psf::TagTimeMs(set_.tags, "length");
    }

    long long FadeMs() const
    {
        return psf::TagTimeMs(set_.tags, "fade");
    }

    // The volume tag's factor, or 1.0 without one.
    double Volume() const
    {
        return psf::TagVolume(set_.tags);
    }

    // Takes a snapshot that shares what it can with `previous`, best the latest snapshot. Returns nullptr when the
    // player isn't playing, or when saving fails. The player then goes on as it was. Only for a player with snapshots.
    template <typename D = Derived>
    std::shared_ptr<const typename D::Snapshot> Save(const typename D::Snapshot* previous = nullptr)
    {
        if (state_ != State::kPlaying && state_ != State::kFinished)
        {
            return nullptr;
        }

        if (previous && (previous->owner != this || previous->run != run_))
        {
            previous = nullptr;
        }

        auto snapshot = std::make_shared<typename D::Snapshot>();
        snapshot->owner = this;
        snapshot->run = run_;
        snapshot->state = state_;
        snapshot->finished_frame = finished_frame_;
        const std::vector<int16_t>& buf = Self().Output();
        snapshot->pending.assign(buf.begin() + static_cast<std::ptrdiff_t>(consumed_), buf.end());
        snapshot->dropped = dropped_ + consumed_;
        try
        {
            snapshot->source = Self().SaveSource(previous ? previous->source.get() : nullptr);
        }
        catch (const std::exception&)
        {
            // Saving only reads the source's state. Playback can go on without the snapshot.
            return nullptr;
        }

        if (!snapshot->source)
        {
            return nullptr;
        }

        return snapshot;
    }

    // Restores `snapshot`, with the samples waiting to be handed out. Returns false if another player or an earlier run
    // took the snapshot, or if the source can't go back to it (see Error()). Only for a player with snapshots.
    template <typename D = Derived>
    bool Restore(const typename D::Snapshot& snapshot)
    {
        if (snapshot.owner != this || snapshot.run != run_ || !Self().CanRestore(snapshot.source.get()))
        {
            return false;
        }

        try
        {
            Self().RestoreSource(*snapshot.source);
        }
        catch (const std::exception& e)
        {
            return Fail(std::string("restoring the emulation's state failed: ") + e.what());
        }

        Self().Output() = snapshot.pending;
        consumed_ = 0;
        dropped_ = snapshot.dropped;
        finished_frame_ = snapshot.finished_frame;
        state_ = snapshot.state;
        error_.clear();

        return true;
    }

protected:
    // Records an error and stops the player. Returns false.
    bool Fail(const std::string& message)
    {
        error_ = message;
        state_ = State::kError;

        return false;
    }

    // Runs the source one step. A step that throws fails with kStepFailure and the exception's message.
    bool Step()
    {
        try
        {
            return Self().RunStep();
        }
        catch (const std::exception& e)
        {
            return Fail(std::string(Derived::kStepFailure) + e.what());
        }
    }

    // For a game mode whose driver reports in a status word: 0 while the game boots, 1 once the sound plays and 2 once
    // it has finished, or bit 31 with the failed step in the low byte. Runs the emulation until the sound plays or has
    // finished, for at most `timeout` emulated cycles, and drops the samples before. Returns false after an error.
    bool RunUntilSoundPlays(uint64_t timeout)
    {
        while (true)
        {
            const uint32_t status = Self().DriverStatus();
            if (status & 0x80000000u)
            {
                return Fail("driver error at step " + std::to_string(status & 0xff));
            }

            if (status == 1 || status == 2)
            {
                return true;
            }

            if (Self().EmulatedCycles() > timeout)
            {
                return Fail("timed out waiting for the sound to start");
            }

            if (!Step())
            {
                return false;
            }

            Self().Output().clear();
        }
    }

    Set set_;
    State state_ = State::kIdle;
    std::string error_;
    std::size_t consumed_ = 0;    // samples of Output() handed out already (two per frame)
    uint64_t dropped_ = 0;        // samples dropped from the front of Output() since the sound started
    uint64_t finished_frame_ = 0; // see FinishedFrame
    uint64_t run_ = 0;            // counts the starts: a snapshot belongs to the run it was taken in

private:
    Derived& Self()
    {
        return static_cast<Derived&>(*this);
    }

    const Derived& Self() const
    {
        return static_cast<const Derived&>(*this);
    }
};

} // namespace xsflib::player
