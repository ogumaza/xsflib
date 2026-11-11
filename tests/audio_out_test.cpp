// SPDX-License-Identifier: MIT

// Tests the command-line tools' audio writer (xsflib/cli/audio_out.h). A WAV file must hold the samples byte for byte
// after a 44-byte header. A FLAC file must decode to the same samples at the same rate. The extension must pick the
// format in any case. A file that can't be written must be reported. A length that a WAV file can't hold must be
// rejected before anything is rendered.
// The test reads the FLAC files with the FLAC reader of the tests (xsflib/testing/flac.h). The reader must refuse a
// file that isn't 16-bit stereo FLAC at the rate, a damaged one and a missing one.
//
// usage: xsflib_audio_out_test

#include "xsflib/cli/audio_out.h"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

#include "xsflib/bytes.h"
#include "xsflib/files.h"
#include "xsflib/testing/check.h"
#include "xsflib/testing/flac.h"
#include "xsflib/testing/temp_folder.h"

using xsflib::testing::Check;

namespace
{

namespace fs = std::filesystem;

constexpr uint32_t kRate = 48000;

// A second and a bit of a sweep in the left channel and noise in the right. The samples reach both ends of the range.
std::vector<int16_t> Signal()
{
    std::vector<int16_t> pcm(2 * 50001);
    uint32_t noise = 1;
    for (std::size_t i = 0; i < pcm.size() / 2; i++)
    {
        pcm[2 * i] = static_cast<int16_t>(static_cast<uint16_t>(i * i / 7));
        noise = noise * 1664525u + 1013904223u;
        pcm[2 * i + 1] = static_cast<int16_t>(static_cast<uint16_t>(noise >> 16));
    }

    pcm[0] = -32768;
    pcm[1] = 32767;

    return pcm;
}

void CheckWav(const fs::path& path, const std::vector<int16_t>& pcm)
{
    std::vector<uint8_t> file;
    const bool read = xsflib::ReadFile(path, file);
    if (!Check(read && file.size() == 44 + 2 * pcm.size(), "the WAV file holds a 44-byte header and the samples"))
    {
        return;
    }

    const uint8_t* p = file.data();
    XSFLIB_CHECK(std::string(p, p + 4) == "RIFF" && xsflib::LoadLe32(p + 4) == file.size() - 8);
    XSFLIB_CHECK(std::string(p + 8, p + 16) == "WAVEfmt " && xsflib::LoadLe32(p + 16) == 16);
    XSFLIB_CHECK(xsflib::LoadLe16(p + 20) == 1 && xsflib::LoadLe16(p + 22) == 2);
    XSFLIB_CHECK(xsflib::LoadLe32(p + 24) == kRate && xsflib::LoadLe32(p + 28) == kRate * 4);
    XSFLIB_CHECK(xsflib::LoadLe16(p + 32) == 4 && xsflib::LoadLe16(p + 34) == 16);
    XSFLIB_CHECK(std::string(p + 36, p + 40) == "data" && xsflib::LoadLe32(p + 40) == 2 * pcm.size());

    bool same = true;
    for (std::size_t i = 0; i < pcm.size(); i++)
    {
        same = same && static_cast<int16_t>(xsflib::LoadLe16(p + 44 + 2 * i)) == pcm[i];
    }
    XSFLIB_CHECK(same);
}

void CheckFlac(const fs::path& path, const std::vector<int16_t>& pcm)
{
    std::vector<int16_t> decoded;
    try
    {
        decoded = xsflib::testing::ReadFlac(path, kRate);
    }
    catch (const std::exception& e)
    {
        Check(false, e.what());
        return;
    }

    Check(decoded == pcm, "the FLAC file decodes to the samples written");
}

// ReadFlac refuses a file that isn't FLAC, a FLAC file at another rate and a file that isn't there.
void CheckFlacRefusals(const fs::path& flac, const fs::path& wav)
{
    const auto refused = [](const fs::path& path, uint32_t rate, const std::string& message)
    {
        try
        {
            xsflib::testing::ReadFlac(path, rate);
        }
        catch (const std::exception& e)
        {
            return std::string(e.what()) == message;
        }

        return false;
    };

    Check(refused(wav, kRate, "can't decode " + xsflib::Utf8(wav)), "a WAV file isn't decoded as FLAC");
    Check(refused(flac, kRate + 1, "can't decode " + xsflib::Utf8(flac)), "a FLAC file at another rate is refused");
    const fs::path missing = flac.parent_path() / "none.flac";
    Check(refused(missing, kRate, "can't read " + xsflib::Utf8(missing)), "a missing file is reported");

    std::vector<uint8_t> bytes;
    XSFLIB_CHECK(xsflib::ReadFile(flac, bytes));
    bytes[bytes.size() - 1] ^= 0xff;
    bool corrupt = false;
    try
    {
        xsflib::testing::DecodeFlac(bytes, kRate, "corrupt");
    }
    catch (const std::exception& e)
    {
        corrupt = std::string(e.what()) == "can't decode corrupt";
    }
    Check(corrupt, "a FLAC stream whose last frame is damaged is refused");
}

// The paths go to the writer as UTF-8.
void TestWriteAudio()
{
    const xsflib::testing::TempFolder temp("xsflib_audio_out_test");
    const fs::path& dir = temp.Path();

    const std::vector<int16_t> pcm = Signal();

    XSFLIB_CHECK(!xsflib::cli::WriteAudio(xsflib::Utf8(dir / "out.wav"), pcm, kRate));
    CheckWav(dir / "out.wav", pcm);
    XSFLIB_CHECK(!xsflib::cli::WriteAudio(xsflib::Utf8(dir / "out.flac"), pcm, kRate));
    CheckFlac(dir / "out.flac", pcm);
    CheckFlacRefusals(dir / "out.flac", dir / "out.wav");

    // The extension picks the format in any case. Anything but .flac is WAV.
    XSFLIB_CHECK(!xsflib::cli::WriteAudio(xsflib::Utf8(dir / "upper.FLAC"), pcm, kRate));
    CheckFlac(dir / "upper.FLAC", pcm);
    XSFLIB_CHECK(!xsflib::cli::WriteAudio(xsflib::Utf8(dir / "other.raw"), pcm, kRate));
    CheckWav(dir / "other.raw", pcm);

    // No samples make a valid file of each kind.
    XSFLIB_CHECK(!xsflib::cli::WriteAudio(xsflib::Utf8(dir / "empty.wav"), {}, kRate));
    CheckWav(dir / "empty.wav", {});
    XSFLIB_CHECK(!xsflib::cli::WriteAudio(xsflib::Utf8(dir / "empty.flac"), {}, kRate));
    CheckFlac(dir / "empty.flac", {});

    // A WAV file's sizes are 32-bit. The audio can take up to 2^32 - 37 bytes, 1,073,741,814 frames. FLAC has no such
    // limit.
    XSFLIB_CHECK(!xsflib::cli::CheckAudioLength("out.wav", 1'073'741'814));
    XSFLIB_CHECK(xsflib::cli::CheckAudioLength("out.wav", 1'073'741'815).has_value());
    XSFLIB_CHECK(!xsflib::cli::CheckAudioLength("out.FLAC", 1'073'741'815));

    // A file in a folder that doesn't exist can't be written.
    XSFLIB_CHECK(xsflib::cli::WriteAudio(xsflib::Utf8(dir / "missing" / "out.wav"), pcm, kRate).has_value());
    XSFLIB_CHECK(xsflib::cli::WriteAudio(xsflib::Utf8(dir / "missing" / "out.flac"), pcm, kRate).has_value());
}

} // namespace

int main()
{
    return xsflib::testing::RunTest("audio out", TestWriteAudio);
}
