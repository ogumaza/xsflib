// SPDX-License-Identifier: MIT

// Checks the helpers for the tests of PSF sets (xsflib/testing/sets.h): the reader of files in memory, the whole output
// of a playback, and the checks of a playback. Each check passes on a right playback and fails on a wrong one.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

#include "xsflib/testing/sets.h"

using xsflib::testing::CheckEnd;
using xsflib::testing::CheckLengthAndFade;
using xsflib::testing::CheckRefusals;
using xsflib::testing::CheckSeeks;
using xsflib::testing::CheckVolume;
using xsflib::testing::failures;

namespace
{

void TestMapReader()
{
    const std::map<std::string, std::vector<uint8_t>> kFiles = {{"dir/lib.tst", {1, 2}}, {"dir/sub/x.minitst", {3}}};
    const xsflib::psf::FileReader reader = xsflib::testing::MapReader(kFiles);
    std::vector<uint8_t> data;

    XSFLIB_CHECK(reader("dir/lib.tst", data) && data == std::vector<uint8_t>({1, 2}));
    // A library's path from a mini file's folder resolves as it would on disk.
    XSFLIB_CHECK(reader("dir/sub/../lib.tst", data) && data == std::vector<uint8_t>({1, 2}));
    XSFLIB_CHECK(reader("dir/./sub/x.minitst", data) && data == std::vector<uint8_t>({3}));
    XSFLIB_CHECK(!reader("dir/missing.tst", data));
}

// The failures that `f`'s checks report. The test takes them back, and keeps their messages out of its output. A given
// `messages` gets the messages.
template <typename F>
int FailuresOf(F f, std::string* messages = nullptr)
{
    std::ostringstream caught;
    std::ostream* const stream = seqlib::testing::failure_stream;
    seqlib::testing::failure_stream = &caught;
    const int before = failures;
    f();
    const int found = failures - before;
    failures = before;
    seqlib::testing::failure_stream = stream;

    if (messages != nullptr)
    {
        *messages = caught.str();
    }

    return found;
}

// A playback of kFrames frames whose samples count up from 1, left before right, or one that doesn't open. The playback
// of "late" lands a frame after each seek's frame.
class CountingPlayback
{
public:
    static constexpr std::size_t kFrames = 7;

    bool Open(const std::string& path, const xsflib::psf::FileReader&)
    {
        error_ = path == "broken" ? "it doesn't open" : "";
        late_ = path == "late";

        return error_.empty();
    }

    bool Start()
    {
        frame_ = 0;

        return true;
    }

    std::size_t Render(int16_t* out, std::size_t frames)
    {
        const std::size_t n = std::min<std::size_t>(frames, kFrames - frame_);
        for (std::size_t i = 0; i < 2 * n; i++)
        {
            out[i] = static_cast<int16_t>(2 * frame_ + i + 1);
        }

        frame_ += n;

        return n;
    }

    bool Seek(uint64_t frame)
    {
        if (frame >= kFrames)
        {
            return false;
        }

        frame_ = static_cast<std::size_t>(frame) + (late_ ? 1 : 0);

        return true;
    }

    uint64_t Position() const
    {
        return frame_;
    }

    std::string Error() const
    {
        return error_;
    }

private:
    std::size_t frame_ = 0;
    bool late_ = false;
    std::string error_;
};

void TestRenderAll()
{
    std::vector<int16_t> expected;
    for (int16_t v = 1; v <= static_cast<int16_t>(2 * CountingPlayback::kFrames); v++)
    {
        expected.push_back(v);
    }

    // Blocks that divide the playback and blocks that don't give the same output.
    XSFLIB_CHECK(xsflib::testing::RenderAll<CountingPlayback>("song", {}, 3) == expected);
    XSFLIB_CHECK(xsflib::testing::RenderAll<CountingPlayback>("song", {}, 7) == expected);
    XSFLIB_CHECK(xsflib::testing::RenderAll<CountingPlayback>("song", {}) == expected);

    // A playback that doesn't open gives nothing, and fails one check that names the playback's error.
    bool empty = false;
    std::string messages;
    const auto render = [&]
    {
        empty = xsflib::testing::RenderAll<CountingPlayback>("broken", {}).empty();
    };

    const int found = FailuresOf(render, &messages);

    XSFLIB_CHECK(empty && found == 1 && messages.find("broken plays: it doesn't open") != std::string::npos);
}

// A model's output of `frames` stereo frames, with samples that differ from each other and from 0.
std::vector<int16_t> Model(std::size_t frames)
{
    std::vector<int16_t> model(2 * frames);
    for (std::size_t i = 0; i < model.size(); i++)
    {
        model[i] = static_cast<int16_t>(i % 2 == 0 ? 1000 + 3 * i : -999 - 5 * i);
    }

    return model;
}

void TestLengthAndFade()
{
    const std::vector<int16_t> model = Model(12);
    std::vector<int16_t> played(model.begin(), model.begin() + 2 * 8);
    for (std::size_t i = 0; i < 2 * 4; i++)
    {
        const double gain = 1.0 - static_cast<double>(i / 2) / 4.0;
        played.push_back(static_cast<int16_t>(std::round(model[2 * 8 + i] * gain)));
    }

    std::vector<int16_t> unfaded = played;
    unfaded[2 * 10] = model[2 * 10];
    std::vector<int16_t> wrong = played;
    wrong[3]++;
    const std::vector<int16_t> short_fade(played.begin(), played.end() - 2);

    XSFLIB_CHECK(FailuresOf([&] { CheckLengthAndFade(played, model, 8, 4); }) == 0);
    XSFLIB_CHECK(FailuresOf([&] { CheckLengthAndFade(unfaded, model, 8, 4); }) == 1);
    XSFLIB_CHECK(FailuresOf([&] { CheckLengthAndFade(wrong, model, 8, 4); }) == 1);
    XSFLIB_CHECK(FailuresOf([&] { CheckLengthAndFade(short_fade, model, 8, 4); }) == 2);
}

void TestEnd()
{
    const std::vector<int16_t> model = Model(20);
    const std::vector<int16_t> played(model.begin(), model.begin() + 2 * (5 + 3));
    std::vector<int16_t> wrong = played;
    wrong.back()++;

    XSFLIB_CHECK(FailuresOf([&] { CheckEnd(played, model, 5, 3); }) == 0);
    XSFLIB_CHECK(FailuresOf([&] { CheckEnd(played, model, 6, 3); }) == 1);
    XSFLIB_CHECK(FailuresOf([&] { CheckEnd(wrong, model, 5, 3); }) == 1);
}

void TestVolume()
{
    const std::vector<int16_t> kPlayed = {100, -100, 3, -3, 32767, -32768};
    const std::vector<int16_t> kHalved = {50, -50, 2, -2, 16384, -16384};

    XSFLIB_CHECK(FailuresOf([&] { CheckVolume(kHalved, kPlayed, 0.5); }) == 0);
    XSFLIB_CHECK(FailuresOf([&] { CheckVolume(kPlayed, kPlayed, 0.5); }) == 1);
    XSFLIB_CHECK(FailuresOf([&] { CheckVolume({50, -50}, kPlayed, 0.5); }) == 1);
}

void TestSeeks()
{
    const std::vector<int16_t> straight = xsflib::testing::RenderAll<CountingPlayback>("song", {});
    std::vector<int16_t> wrong = straight;
    wrong[2 * 1]++;

    // A seek forward and a seek back, rendering 2 frames after each.
    XSFLIB_CHECK(FailuresOf([&] { CheckSeeks<CountingPlayback>("song", {}, straight, {4, 1}, 2); }) == 0);
    XSFLIB_CHECK(FailuresOf([&] { CheckSeeks<CountingPlayback>("late", {}, straight, {4, 1}, 2); }) == 1);
    XSFLIB_CHECK(FailuresOf([&] { CheckSeeks<CountingPlayback>("song", {}, wrong, {4, 1}, 2); }) == 1);

    // A seek past the end fails. So does one that leaves too few frames to render.
    XSFLIB_CHECK(FailuresOf([&] { CheckSeeks<CountingPlayback>("song", {}, straight, {9}, 2); }) == 1);
    XSFLIB_CHECK(FailuresOf([&] { CheckSeeks<CountingPlayback>("song", {}, straight, {6}, 2); }) == 1);
}

void TestRefusals()
{
    XSFLIB_CHECK(FailuresOf([] { CheckRefusals<CountingPlayback>({{"broken", "doesn't open"}}, {}); }) == 0);
    XSFLIB_CHECK(FailuresOf([] { CheckRefusals<CountingPlayback>({{"broken", "is broken"}}, {}); }) == 1);
    XSFLIB_CHECK(FailuresOf([] { CheckRefusals<CountingPlayback>({{"song", ""}}, {}); }) == 1);
    XSFLIB_CHECK(
        FailuresOf(
            [] { CheckRefusals<CountingPlayback>({{"broken", "doesn't open"}, {"song", "doesn't open"}}, {}); }) == 1);
}

} // namespace

int main()
{
    TestMapReader();
    TestRenderAll();
    TestLengthAndFade();
    TestEnd();
    TestVolume();
    TestSeeks();
    TestRefusals();

    return xsflib::testing::Summary("testing sets");
}
