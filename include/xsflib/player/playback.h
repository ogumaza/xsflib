// SPDX-License-Identifier: MIT

// Playback of a set as a finite (or endless) stream: length, fade and volume from the tags or the defaults, end
// detection and seeking. A format's play tool, foobar2000 component and XMPlay plugin share it. They play a file
// identically.
//
// A player that can take snapshots of its emulation (HasSnapshots) seeks with them. While rendering, the playback takes
// a snapshot every few seconds (see the player's Save). A seek goes back to the latest snapshot before the target and
// renders from there. A player without snapshots renders on from the current position, or from the start when the
// target lies before it.
//
// The player type provides kSampleRate, Load (from a path and a reader, or from a loaded set), Start, Render, Volume,
// LengthMs, FadeMs, GetState with State::kPlaying, kFinished and kError, FinishedFrame, GetTags and Error. A player
// with snapshots also provides a Snapshot type, Save and Restore.

#pragma once

#include <algorithm>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "xsflib/psf/psf.h"

namespace xsflib::player
{

// Whether `Player` can take snapshots of its emulation and restore them.
template <typename Player>
concept HasSnapshots = requires { typename Player::Snapshot; };

// The type of a player's snapshots, or void for a player that takes none.
template <typename Player>
struct SnapshotOf
{
    using Type = void;
};

template <HasSnapshots Player>
struct SnapshotOf<Player>
{
    using Type = typename Player::Snapshot;
};

// How a Playback plays: the length and fade of a file without a length tag, endless play, overrides of the tags, and
// the snapshots for seeks.
template <typename Player>
struct PlaybackOptions
{
    // The defaults of default_length_ms and default_fade_ms. A play tool's help, a foobar2000 component's preferences
    // and an XMPlay plugin's options page show them in whole seconds.
    static constexpr long long kDefaultLengthMs = 150000;
    static constexpr long long kDefaultFadeMs = 10000;
    static_assert(kDefaultLengthMs % 1000 == 0 && kDefaultFadeMs % 1000 == 0);

    // Length and fade used when a file has no length tag.
    long long default_length_ms = kDefaultLengthMs;
    long long default_fade_ms = kDefaultFadeMs;

    // Whether playback is endless. Endless playback ignores the length and fade, and doesn't stop at the end of the
    // sound.
    bool endless = false;

    // Overrides of the length and fade tags (ignored when negative).
    long long length_override_ms = -1;
    long long fade_override_ms = -1;

    // Whether the playback takes snapshots while rendering. Snapshots make seeks quick. They cost a little time and
    // memory. A render that never seeks can save that. A player without snapshots ignores this and the two settings
    // below.
    bool snapshots = true;

    // Initial interval between snapshots, in frames: a seek renders up to this much. Shorter intervals reduce rendering
    // time when seeking. They cost more time and memory spent saving states.
    uint64_t snapshot_interval = 2 * static_cast<uint64_t>(Player::kSampleRate);

    // Maximum number of snapshots. Past it, the playback doubles the interval and discards every other snapshot. Memory
    // use then stays bounded for long or endless tracks.
    std::size_t max_snapshots = 128;
};

// A file's play time before its fade, and the fade's length.
struct TrackLength
{
    long long play_ms = 0;
    long long fade_ms = 0;
};

// The length for a file whose length and fade tags give `length_ms` and `fade_ms` (negative when a tag is missing).
// Without a length tag, the file gets the default length. It also gets the default fade unless it has a fade tag.
template <typename Player>
TrackLength ResolveLength(long long length_ms, long long fade_ms, const PlaybackOptions<Player>& options)
{
    if (length_ms < 0)
    {
        return {options.default_length_ms, fade_ms >= 0 ? fade_ms : options.default_fade_ms};
    }

    return {length_ms, fade_ms >= 0 ? fade_ms : 0};
}

// Plays a set through `Player`. Open loads the set, and Start starts it. Render writes its stereo frames with the
// length, fade and volume applied. Seek moves to a frame.
template <typename Player>
class Playback
{
public:
    static constexpr int kSampleRate = Player::kSampleRate;

    // Loads a file and its libraries, without starting the player.
    bool Open(const std::string& path, const psf::FileReader& reader, const PlaybackOptions<Player>& options = {})
    {
        return OpenWith(options, [&] { return player_.Load(path, reader); });
    }

    // Uses an already assembled set.
    template <typename Set>
        requires(!std::convertible_to<Set, std::string>)
    bool Open(Set set, const PlaybackOptions<Player>& options)
    {
        return OpenWith(options, [&] { return player_.Load(std::move(set)); });
    }

    // Starts (or restarts) from the beginning.
    bool Start()
    {
        position_ = 0;
        finished_at_.reset();
        DropSnapshots();
        interval_ = std::max<uint64_t>(options_.snapshot_interval, 1);

        if (!player_.Start())
        {
            return false;
        }

        SaveIfDue();

        return true;
    }

    // Renders up to `frames` stereo frames (interleaved L, R, 16-bit) with fade and volume applied. Returns the number
    // of frames written: fewer than requested at the end of the track, 0 after it or after an error (see Error()).
    std::size_t Render(int16_t* out, std::size_t frames)
    {
        std::size_t n = frames;
        if (!options_.endless)
        {
            uint64_t end = play_frames_ + fade_frames_;

            // A sound without a length tag ends half a second after it reports that it finished.
            if (!tagged_ && finished_at_)
            {
                end = std::min(end, *finished_at_ + kSampleRate / 2);
            }

            if (position_ >= end)
            {
                return 0;
            }

            n = static_cast<std::size_t>(std::min<uint64_t>(n, end - position_));
        }

        const std::size_t got = player_.Render(out, n);
        if (got == 0)
        {
            return 0;
        }

        const double volume = player_.Volume();
        const bool fading = !options_.endless && position_ + got > play_frames_ && fade_frames_ > 0;
        if (volume != 1.0 || fading)
        {
            for (std::size_t i = 0; i < got; i++)
            {
                double gain = volume;
                const uint64_t f = position_ + i;
                if (fading && f >= play_frames_)
                {
                    gain *= 1.0 - static_cast<double>(f - play_frames_) / static_cast<double>(fade_frames_);
                }
                for (int c = 0; c < 2; c++)
                {
                    const double v = std::round(out[2 * i + c] * gain);
                    out[2 * i + c] = static_cast<int16_t>(std::clamp(v, -32768.0, 32767.0));
                }
            }
        }

        position_ += got;
        if (!tagged_ && !finished_at_ && player_.GetState() == Player::State::kFinished)
        {
            finished_at_ = player_.FinishedFrame();
        }

        SaveIfDue();

        return got;
    }

    // Seeks to `frame` by rendering from the latest snapshot at or before it, or from the current position if nearer.
    // Within previously rendered audio, this requires less than one snapshot interval of rendering (PlaybackOptions).
    // Seeking further ahead renders the remaining distance. Without snapshots, backward seeks restart the player. Polls
    // `abort` between blocks and returns false if it requests cancellation.
    bool Seek(uint64_t frame, const std::function<bool()>& abort = {})
    {
        const Route route = RouteTo(frame);
        if (route.from)
        {
            if constexpr (HasSnapshots<Player>)
            {
                if (player_.Restore(*route.from->snapshot))
                {
                    position_ = route.from->position;
                    finished_at_ = route.from->finished_at;
                    latest_ = route.from->snapshot;
                }
                else if (!Start())
                {
                    return false;
                }
            }
        }
        else if (route.restart && !Start())
        {
            return false;
        }

        scratch_.resize(kSeekBlock * 2);
        while (position_ < frame)
        {
            if (abort && abort())
            {
                return false;
            }

            const std::size_t n = static_cast<std::size_t>(std::min<uint64_t>(kSeekBlock, frame - position_));
            if (Render(scratch_.data(), n) == 0)
            {
                break; // the end of the track
            }
        }

        return true;
    }

    // Returns the frame a seek to `frame` starts rendering from: the position of the snapshot it restores, the current
    // position if that's nearer, or 0 if the player must restart.
    uint64_t SeekStart(uint64_t frame) const
    {
        const Route route = RouteTo(frame);
        return route.from ? route.from->position : route.restart ? 0 : position_;
    }

    // Enables or disables snapshots. Disabling them discards existing snapshots.
    void SetSnapshots(bool on)
    {
        options_.snapshots = on;
        if (!on)
        {
            DropSnapshots();
        }
    }

    // Number of retained snapshots, for tests.
    std::size_t SnapshotCount() const
    {
        return saved_.size();
    }

    // Current position in frames.
    uint64_t Position() const
    {
        return position_;
    }

    // Frames before the fade starts. Endless playback ignores this and FadeFrames.
    uint64_t PlayFrames() const
    {
        return play_frames_;
    }

    // The fade's length in frames.
    uint64_t FadeFrames() const
    {
        return fade_frames_;
    }

    // Total length in frames (0 when endless).
    uint64_t LengthFrames() const
    {
        return options_.endless ? 0 : play_frames_ + fade_frames_;
    }

    // True when the length comes from the file's tags or the options rather than the defaults.
    bool HasLengthTag() const
    {
        return tagged_;
    }

    decltype(auto) GetTags() const
    {
        return player_.GetTags();
    }

    decltype(auto) Error() const
    {
        return player_.Error();
    }

    Player& GetPlayer()
    {
        return player_;
    }

private:
    using Snapshot = typename SnapshotOf<Player>::Type;

    // A snapshot, and where playback was when it was taken.
    struct SavedState
    {
        uint64_t position = 0;
        std::optional<uint64_t> finished_at;
        std::shared_ptr<const Snapshot> snapshot;
    };

    // How a seek to `frame` gets there. It restores `from` if that's set. Otherwise it restarts if `restart` is set, or
    // renders on from the current position.
    struct Route
    {
        const SavedState* from = nullptr;
        bool restart = false;
    };

    static constexpr std::size_t kSeekBlock = 4096; // frames rendered at a time while seeking

    static uint64_t MsToFrames(long long ms)
    {
        return ms <= 0 ? 0 : static_cast<uint64_t>(ms) * kSampleRate / 1000;
    }

    // Takes `options`, drops the snapshots of the set before, and loads a set with `load`. The length then follows from
    // the new set's tags and the options.
    template <typename Load>
    bool OpenWith(const PlaybackOptions<Player>& options, const Load& load)
    {
        options_ = options;
        DropSnapshots();

        if (!load())
        {
            return false;
        }

        ComputeLength();

        return true;
    }

    // Drops every snapshot, the latest one included.
    void DropSnapshots()
    {
        saved_.clear();
        latest_.reset();
    }

    void ComputeLength()
    {
        // Without a length tag, the defaults apply unless the sound ends by itself first (see Render).
        const long long length = options_.length_override_ms >= 0 ? options_.length_override_ms : player_.LengthMs();
        const long long fade = options_.fade_override_ms >= 0 ? options_.fade_override_ms : player_.FadeMs();
        tagged_ = length >= 0;

        const TrackLength resolved = ResolveLength(length, fade, options_);
        play_frames_ = MsToFrames(resolved.play_ms);
        fade_frames_ = MsToFrames(resolved.fade_ms);
    }

    Route RouteTo(uint64_t frame) const
    {
        // Use the latest snapshot at or before the target. When the current position is nearer and the player can
        // continue from it, continue from there instead.
        const bool can_go_on =
            player_.GetState() == Player::State::kPlaying || player_.GetState() == Player::State::kFinished;
        const auto it = std::ranges::upper_bound(saved_, frame, {}, &SavedState::position);
        const SavedState* from = it == saved_.begin() ? nullptr : &*std::prev(it);
        if (from && (!can_go_on || frame < position_ || from->position > position_))
        {
            return {from, false};
        }

        return {nullptr, !can_go_on || frame < position_};
    }

    // Takes a snapshot if none has been taken yet in this stretch of interval_ frames.
    void SaveIfDue()
    {
        if constexpr (HasSnapshots<Player>)
        {
            if (!options_.snapshots)
            {
                return;
            }

            const uint64_t stretch = position_ / interval_;
            const auto it = std::ranges::upper_bound(saved_, position_, {}, &SavedState::position);
            if (it != saved_.begin() && std::prev(it)->position / interval_ == stretch)
            {
                return;
            }

            std::shared_ptr<const Snapshot> snapshot = player_.Save(latest_.get());
            if (!snapshot)
            {
                return;
            }

            latest_ = snapshot;
            saved_.insert(it, {position_, finished_at_, std::move(snapshot)});

            // Double the interval and keep the first snapshot in each new interval.
            while (saved_.size() > std::max<std::size_t>(options_.max_snapshots, 1))
            {
                interval_ *= 2;
                std::vector<SavedState> kept;
                for (SavedState& s : saved_)
                {
                    if (kept.empty() || kept.back().position / interval_ != s.position / interval_)
                    {
                        kept.push_back(std::move(s));
                    }
                }

                saved_ = std::move(kept);
            }
        }
    }

    Player player_;
    PlaybackOptions<Player> options_;
    bool tagged_ = false;
    uint64_t play_frames_ = 0;
    uint64_t fade_frames_ = 0;
    uint64_t position_ = 0;
    std::optional<uint64_t> finished_at_; // the frame at which the sound reported its end (untagged files)
    std::vector<int16_t> scratch_;

    // Declared after player_ so snapshots release their references to the player's objects before the player is
    // destroyed.
    std::vector<SavedState> saved_; // in order of position, at most one in each stretch of interval_ frames

    // Last snapshot taken or restored. The next snapshot shares pages with it. That holds even if it is no longer in
    // saved_.
    std::shared_ptr<const Snapshot> latest_;

    uint64_t interval_ = 0;
};

} // namespace xsflib::player
