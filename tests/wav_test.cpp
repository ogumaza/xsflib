// SPDX-License-Identifier: MIT

// Checks xsflib's WAV files (xsflib/wav.h): the bytes of a stereo file, and mono files the same byte for byte as those
// of seqlib::WavFile. The two libraries keep separate WAV writers. A fix to one of them fails this test until the other
// matches.

#include "xsflib/wav.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "seqlib/wav.h"

#include "xsflib/testing/check.h"

using xsflib::testing::Check;

int main()
{
    const std::vector<int16_t> kSamples = {1, -2, 0x7FFF, -0x8000};

    // Two stereo frames at 48 kHz.
    const std::string kExpected(
        "RIFF\x2c\0\0\0WAVEfmt \x10\0\0\0\x01\0\x02\0\x80\xbb\0\0\0\xee\x02\0"
        "\x04\0\x10\0data\x08\0\0\0\x01\0\xfe\xff\xff\x7f\0\x80",
        52);

    const std::vector<uint8_t> stereo = xsflib::WavFile(kSamples, 2, 48000);

    Check(std::string(stereo.begin(), stereo.end()) == kExpected, "a WAV file of two stereo frames");

    // Mono files, against seqlib's writer. seqlib::WavFile also takes a seqlib::Pcm. A named span picks the overload
    // that takes samples, as xsflib::WavFile does.
    Check(xsflib::WavFile(kSamples, 1, 32000) == seqlib::WavFile(kSamples, 32000), "four mono samples match seqlib");
    const std::span<const int16_t> kNoSamples;
    Check(xsflib::WavFile(kNoSamples, 1, 22050) == seqlib::WavFile(kNoSamples, 22050), "an empty file matches seqlib");

    return xsflib::testing::Summary("wav");
}
