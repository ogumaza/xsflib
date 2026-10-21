// SPDX-License-Identifier: MIT

// Helpers for the tests of PSF sets: a set's files held in memory, the loader's error for a set, a render's frames, the
// whole of a set's playback, and checks of a playback's length, fade, end, volume and seeks, and of the sets that a
// playback rejects.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "xsflib/psf/psf.h"
#include "xsflib/psf/set.h"
#include "xsflib/testing/check.h"

namespace xsflib::testing
{

// A reader of `files`, a set's files by path. Reading a path gives the file that `files` holds under the path's
// lexically normal form, with / between its parts. The paths of a set's libraries then resolve as they do on disk. The
// reader keeps a reference to `files`. `files` has to outlive the reader.
inline psf::FileReader MapReader(const std::map<std::string, std::vector<uint8_t>>& files)
{
    return [&files](const std::string& path, std::vector<uint8_t>& out)
    {
        const auto it = files.find(std::filesystem::path(path).lexically_normal().generic_string());
        if (it == files.end())
        {
            return false;
        }

        out = it->second;

        return true;
    };
}

// The error that loading the set of `Format` at `path` with `reader` gives, or an empty string if the set loads.
template <typename Format>
std::string LoadError(const std::string& path, const psf::FileReader& reader)
{
    psf::LoadedSet<Format> set;
    return psf::LoadSet(path, reader, set).value_or("");
}

// Reads stereo frames from `read`, `block` at a time, until `read` gives no frames or `limit` frames have been read.
// `read` is a call (int16_t* out, std::size_t frames) -> std::size_t that writes up to `frames` frames to `out` and
// returns how many it wrote. A playback's Render and a RenderAhead's Read are calls of that kind.
template <typename Read>
std::vector<int16_t> RenderFrames(Read read, std::size_t limit = SIZE_MAX, std::size_t block = 1024)
{
    std::vector<int16_t> pcm;
    std::size_t done = 0;
    while (done < limit)
    {
        const std::size_t ask = std::min(block, limit - done);
        pcm.resize(2 * (done + ask));
        const std::size_t got = read(pcm.data() + 2 * done, ask);
        if (got == 0)
        {
            break;
        }

        done += got;
    }

    pcm.resize(2 * done);

    return pcm;
}

// The whole output of a `Playback` of the set at `path`, read with `reader`, in stereo frames rendered `block` at a
// time. A playback that doesn't open and start fails a check with its error, and gives nothing.
template <typename Playback>
std::vector<int16_t> RenderAll(const std::string& path, const psf::FileReader& reader, std::size_t block = 1000)
{
    Playback playback;
    const bool started = playback.Open(path, reader) && playback.Start();
    if (!Check(started, path + " plays: " + playback.Error()))
    {
        return {};
    }

    return RenderFrames([&playback](int16_t* out, std::size_t frames) { return playback.Render(out, frames); },
                        SIZE_MAX, block);
}

// Checks the playback of a set with a tagged length and fade, in stereo frames. `played` holds `length` frames of
// `model`, the model's output. The next `fade` frames are those of `model`, scaled by a gain that falls linearly from 1
// toward 0, and rounded.
inline void CheckLengthAndFade(const std::vector<int16_t>& played, const std::vector<int16_t>& model,
                               std::size_t length, std::size_t fade)
{
    const std::size_t total = 2 * (length + fade);
    Check(played.size() == total, "the tagged set plays for its length and fade");
    Check(played.size() >= 2 * length && model.size() >= 2 * length &&
              std::equal(played.begin(), played.begin() + static_cast<std::ptrdiff_t>(2 * length), model.begin()),
          "the tagged set plays the model's output");

    bool faded = played.size() == total && model.size() >= total;
    for (std::size_t i = 0; faded && i < 2 * fade; ++i)
    {
        const double gain = 1.0 - static_cast<double>(i / 2) / static_cast<double>(fade);
        faded = played[2 * length + i] == static_cast<int16_t>(std::round(model[2 * length + i] * gain));
    }
    Check(faded, "the fade falls linearly over its frames");
}

// Checks the playback of a set without a length, in stereo frames. `played` is `model`, the model's output, up to frame
// `end` and `tail` frames more. The model's sound ended at frame `end`.
inline void CheckEnd(const std::vector<int16_t>& played, const std::vector<int16_t>& model, std::size_t end,
                     std::size_t tail)
{
    Check(end > 0 && played.size() == 2 * (end + tail), "the set without a length ends its tail after its sound");
    Check(model.size() >= played.size() && std::equal(played.begin(), played.end(), model.begin()),
          "the set without a length plays the model's output");
}

// Checks that each sample of `scaled` is the sample of `played` times `volume`, rounded.
inline void CheckVolume(const std::vector<int16_t>& scaled, const std::vector<int16_t>& played, double volume)
{
    bool same = scaled.size() == played.size();
    for (std::size_t i = 0; same && i < scaled.size(); ++i)
    {
        same = scaled[i] == static_cast<int16_t>(std::round(played[i] * volume));
    }
    Check(same, "the volume tag scales every sample");
}

// Checks seeks in a `Playback` of the set at `path`, read with `reader`: a seek to each of `frames` in turn, such as
// one forward and then one back. After each seek, the position is the frame, and the next `block` stereo frames are
// those of `straight`, the set's whole output rendered from the start.
template <typename Playback>
void CheckSeeks(const std::string& path, const psf::FileReader& reader, const std::vector<int16_t>& straight,
                const std::vector<uint64_t>& frames, std::size_t block = 1000)
{
    Playback playback;
    std::vector<int16_t> buffer(2 * block);
    bool landed = playback.Open(path, reader) && playback.Start();
    for (const uint64_t frame : frames)
    {
        landed = landed && playback.Seek(frame) && playback.Position() == frame &&
                 playback.Render(buffer.data(), block) == block && straight.size() >= 2 * (frame + block) &&
                 std::equal(buffer.begin(), buffer.end(), straight.begin() + static_cast<std::ptrdiff_t>(2 * frame));
    }
    Check(landed, path + ": seeks land where a straight render does");
}

// Checks that each set in `refusals`, by path, fails to open or to start in a `Playback`, read with `reader`. The
// playback's error has to hold the words given for the set.
template <typename Playback>
void CheckRefusals(const std::map<std::string, std::string>& refusals, const psf::FileReader& reader)
{
    for (const auto& [path, words] : refusals)
    {
        Playback playback;
        const bool refused = !(playback.Open(path, reader) && playback.Start());
        const std::string error = playback.Error();
        Check(refused && error.find(words) != std::string::npos, path + " is rejected for its reason: " + error);
    }
}

} // namespace xsflib::testing
