// SPDX-License-Identifier: MIT

// Tests of the descriptors' checks: each message, word for word, and the order of the header's checks. And the fields
// that every archive descriptor has, written, read and checked for a made-up format.
// The test also checks the lists of track volumes.

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "xsflib/psf/descriptor.h"
#include "xsflib/psf/psf.h"
#include "xsflib/testing/check.h"

namespace
{

using xsflib::psf::CheckArchiveStart;
using xsflib::psf::CheckDescriptorFlags;
using xsflib::psf::CheckDescriptorHeader;
using xsflib::psf::CheckDescriptorOutputMode;
using xsflib::psf::CheckDescriptorVersion;
using xsflib::psf::kDescriptorFlagDspRequired;
using xsflib::psf::SoundTrackVolumes;

void Expect(const std::optional<std::string>& got, const std::optional<std::string>& want, const std::string& what)
{
    xsflib::testing::Check(got == want, what + ": got \"" + got.value_or("nothing") + "\"");
}

// A made-up format's archive descriptor, with one word of its own between the start and the paths.
struct TestArchiveDescriptor
{
    static constexpr uint32_t kMagic = 0x41545358; // "XSTA"

    uint32_t version = 1;
    uint32_t flags = 0;
    uint32_t output_mode = 1;
    uint32_t voice_count = 96;
    uint32_t extra = 0;
    std::string archive_path;
    std::vector<std::string> group_paths;
};

void TestArchiveFields()
{
    using xsflib::psf::ReadArchivePaths;
    using xsflib::psf::ReadArchiveStart;
    using xsflib::testing::Check;

    TestArchiveDescriptor d;
    d.voice_count = 24;
    d.extra = 0x01020304;
    d.archive_path = "a.bin";
    d.group_paths = {"g1", "group2"};
    const std::vector<uint8_t> kWant = {
        'X', 'S', 'T', 'A',                     // the magic
        1,   0,   0,   0,                       // the version
        0,   0,   0,   0,                       // the flags
        1,   0,   0,   0,                       // the output mode
        24,  0,   0,   0,                       // the voice count
        4,   3,   2,   1,                       // the format's word
        5,   0,   'a', '.', 'b', 'i', 'n',      // the archive's path
        2,   0,                                 // two group files
        2,   0,   'g', '1',                     // the first group file's path
        6,   0,   'g', 'r', 'o', 'u', 'p', '2', // the second group file's path
        0,   0,   0,                            // zeros to 48 bytes
    };
    std::vector<uint8_t> v;

    xsflib::psf::PutArchiveStart(v, d);
    xsflib::psf::Put32(v, d.extra);
    xsflib::psf::PutArchivePaths(v, d);

    Check(v == kWant, "an archive descriptor's bytes");

    TestArchiveDescriptor back;
    std::size_t pos = 24;

    const bool read = ReadArchiveStart(v, 24, back) && ReadArchivePaths(v, pos, back);

    Check(read && back.version == 1 && back.flags == 0 && back.output_mode == 1 && back.voice_count == 24 &&
              back.archive_path == "a.bin" && back.group_paths == d.group_paths && pos == 45,
          "an archive descriptor read back");

    // Short data, another magic, and paths cut at every byte.
    TestArchiveDescriptor ignored;
    std::vector<uint8_t> other = v;
    other[0] = 'Y';

    Check(!ReadArchiveStart(std::vector<uint8_t>(v.begin(), v.begin() + 23), 24, ignored),
          "a descriptor shorter than its words");
    Check(!ReadArchiveStart(other, 24, ignored), "another format's magic");
    for (std::size_t n = 24; n < 45; ++n)
    {
        TestArchiveDescriptor part;
        std::size_t at = 24;
        Check(!ReadArchivePaths(std::vector<uint8_t>(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(n)), at, part),
              "paths cut after " + std::to_string(n) + " bytes");
    }

    // CheckArchiveStart checks the header first, then the voice count against the limit it's given.
    const TestArchiveDescriptor kGood;
    TestArchiveDescriptor none = kGood;
    none.voice_count = 0;
    TestArchiveDescriptor many = kGood;
    many.voice_count = 97;
    TestArchiveDescriptor later = many;
    later.version = 2;
    TestArchiveDescriptor flagged = kGood;
    flagged.flags = 1;

    Expect(CheckArchiveStart(kGood, 96), std::nullopt, "a version 1 archive descriptor");
    Expect(CheckArchiveStart(none, 96), "the archive descriptor's voice count is 0; it must be 1 to 96", "no voices");
    Expect(CheckArchiveStart(many, 96), "the archive descriptor's voice count is 97; it must be 1 to 96",
           "too many voices");
    Expect(CheckArchiveStart(many, 128), std::nullopt, "a higher limit");
    Expect(CheckArchiveStart(later, 96), "archive descriptor version 2; this player supports version 1",
           "the header before the voice count");
    Expect(CheckArchiveStart(flagged, 96), "the archive descriptor's flags are 0x1; version 1 has none", "no flags");
}

// A list of track volumes: written, read back, found by sound and checked.
void TestTrackVolumes()
{
    SoundTrackVolumes first;
    first.sound = 0x0100030b;
    first.volumes[1] = 0.0f;
    first.volumes[15] = 0.25f;
    SoundTrackVolumes second;
    second.sound = 0x01000007;
    second.volumes.fill(0.0f);
    second.volumes[3] = 2.0f;
    const std::vector<SoundTrackVolumes> sounds = {first, second};

    // The count, then the item ID and the volumes of each sound. Reading stops at the end of the list.
    std::vector<uint8_t> v = {0xaa};
    xsflib::psf::PutTrackVolumes(v, sounds);
    v.push_back(0xbb);
    XSFLIB_CHECK(v.size() == 1 + 4 + 2 * xsflib::psf::kSoundTrackVolumesBytes + 1 &&
                 xsflib::psf::kSoundTrackVolumesBytes == 68);
    XSFLIB_CHECK(v[1] == 2 && v[2] == 0 && v[5] == 0x0b && v[6] == 0x03 && v[7] == 0x00 && v[8] == 0x01);
    XSFLIB_CHECK(v[9] == 0x00 && v[10] == 0x00 && v[11] == 0x80 && v[12] == 0x3f); // 1.0f

    std::size_t pos = 1;
    std::vector<SoundTrackVolumes> back;
    XSFLIB_CHECK(xsflib::psf::ReadTrackVolumes(v, pos, back) && back == sounds && pos == v.size() - 1);

    // A list cut short, a count larger than the bytes allow, and a position past the end.
    std::vector<SoundTrackVolumes> none;
    std::size_t at = 1;
    XSFLIB_CHECK(!xsflib::psf::ReadTrackVolumes(std::vector<uint8_t>(v.begin(), v.end() - 2), at, none));
    std::vector<uint8_t> large = v;
    large[1] = 3;
    at = 1;
    XSFLIB_CHECK(!xsflib::psf::ReadTrackVolumes(large, at, none) && none.empty());
    at = v.size() + 1;
    XSFLIB_CHECK(!xsflib::psf::ReadTrackVolumes(v, at, none));

    // An empty list is its count alone.
    std::vector<uint8_t> empty;
    xsflib::psf::PutTrackVolumes(empty, {});
    at = 0;
    XSFLIB_CHECK(empty == std::vector<uint8_t>(4, 0) && xsflib::psf::ReadTrackVolumes(empty, at, none) &&
                 none.empty() && at == 4);

    const SoundTrackVolumes* found = xsflib::psf::FindTrackVolumes(sounds, 0x01000007);
    XSFLIB_CHECK(found == &sounds[1] && !xsflib::psf::FindTrackVolumes(sounds, 0x01000008) &&
                 !xsflib::psf::FindTrackVolumes({}, 0x0100030b));

    // Each sound once, and each volume finite and 0 or more.
    std::vector<SoundTrackVolumes> twice = sounds;
    twice[1].sound = first.sound;
    std::vector<SoundTrackVolumes> negative = sounds;
    negative[0].volumes[2] = -0.5f;
    std::vector<SoundTrackVolumes> nan = sounds;
    nan[1].volumes[0] = std::numeric_limits<float>::quiet_NaN();
    std::vector<SoundTrackVolumes> infinite = sounds;
    infinite[1].volumes[5] = std::numeric_limits<float>::infinity();

    Expect(xsflib::psf::CheckTrackVolumes(sounds), std::nullopt, "good track volumes");
    Expect(xsflib::psf::CheckTrackVolumes({}), std::nullopt, "no track volumes");
    Expect(xsflib::psf::CheckTrackVolumes(twice),
           "the archive descriptor lists the track volumes of sound 0x0100030b more than once", "a sound twice");
    Expect(xsflib::psf::CheckTrackVolumes(negative),
           "a track volume of sound 0x0100030b in the archive descriptor isn't a finite number of 0 or more",
           "a negative volume");
    Expect(xsflib::psf::CheckTrackVolumes(nan),
           "a track volume of sound 0x01000007 in the archive descriptor isn't a finite number of 0 or more", "NaN");
    Expect(xsflib::psf::CheckTrackVolumes(infinite),
           "a track volume of sound 0x01000007 in the archive descriptor isn't a finite number of 0 or more",
           "infinity");
}

} // namespace

int main()
{
    const auto tests = []
    {
        Expect(CheckDescriptorVersion("process", 1, 1), std::nullopt, "version 1");
        Expect(CheckDescriptorVersion("archive", 2, 1), "archive descriptor version 2; this player supports version 1",
               "version 2");

        Expect(CheckDescriptorFlags("archive", 0, 0), std::nullopt, "no flags");
        Expect(CheckDescriptorFlags("process", 4, 0), "the process descriptor's flags are 0x4; version 1 has none",
               "a flag where none is allowed");
        Expect(CheckDescriptorFlags("archive", 1, kDescriptorFlagDspRequired), std::nullopt, "DSP required");
        Expect(CheckDescriptorFlags("archive", 0, kDescriptorFlagDspRequired),
               "the archive descriptor's flags are 0x0; version 1 requires only bit 0 (DSP required) to be set",
               "DSP required missing");

        Expect(CheckDescriptorOutputMode("archive", 1), std::nullopt, "stereo");
        Expect(CheckDescriptorOutputMode("archive", 2),
               "the archive descriptor's output mode is 2; version 1 supports only stereo (1)", "another output mode");

        // CheckDescriptorHeader checks the version first, then the flags, then the output mode.
        Expect(CheckDescriptorHeader("archive", 1, 0, 1, 0), std::nullopt, "a good header");
        Expect(CheckDescriptorHeader("archive", 3, 7, 5, 0),
               "archive descriptor version 3; this player supports version 1", "the version first");
        Expect(CheckDescriptorHeader("archive", 1, 7, 5, 0),
               "the archive descriptor's flags are 0x7; version 1 has none", "the flags next");
        Expect(CheckDescriptorHeader("process", 1, 1, 5, kDescriptorFlagDspRequired),
               "the process descriptor's output mode is 5; version 1 supports only stereo (1)", "the output mode last");

        TestArchiveFields();
        TestTrackVolumes();
    };

    return xsflib::testing::RunTest("psf descriptor", tests);
}
