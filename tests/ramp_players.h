// SPDX-License-Identifier: MIT

// Test players for Playback (xsflib/player/playback.h), for the tests of the player framework and of its checks. They
// render a ramp in which every sample shows the frame it came from. One player takes snapshots, and one doesn't. A
// template makes the rate of either player depend on the set.

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace ramp_players
{

// The sample that a ramp player renders at `frame`.
inline int16_t RampSample(uint64_t frame)
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

} // namespace ramp_players
