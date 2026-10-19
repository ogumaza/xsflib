// SPDX-License-Identifier: MIT

// Tests of the program of a format that holds each of a set's files once (streaming.h), and of loading a set of such a
// format (set.h).

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <vector>

#include <zlib.h>

#include "xsflib/bytes.h"
#include "xsflib/psf/program.h"
#include "xsflib/psf/psf.h"
#include "xsflib/psf/set.h"
#include "xsflib/psf/streaming.h"
#include "xsflib/testing/check.h"
#include "xsflib/testing/sets.h"

using namespace xsflib;
using namespace xsflib::psf;
using xsflib::testing::Check;
using xsflib::testing::failures;
using xsflib::testing::LoadError;
using xsflib::testing::MapReader;

namespace
{

constexpr uint8_t kVersion = 0x7d;

// A PSF file as one call of zlib's compress2 makes it. WritePsf compresses a program in pieces. The test checks that
// WritePsf gives the same bytes.
std::vector<uint8_t> OneCallPsf(const std::vector<uint8_t>& reserved, const std::vector<uint8_t>& program,
                                const Tags& tags)
{
    std::vector<uint8_t> compressed;
    uLongf size = 0;
    if (!program.empty())
    {
        size = compressBound(static_cast<uLong>(program.size()));
        compressed.resize(size);
        compress2(compressed.data(), &size, program.data(), static_cast<uLong>(program.size()), Z_BEST_COMPRESSION);
        compressed.resize(size);
    }

    std::vector<uint8_t> out(16);
    std::memcpy(out.data(), "PSF", 3);
    out[3] = kVersion;
    StoreLe32(&out[4], static_cast<uint32_t>(reserved.size()));
    StoreLe32(&out[8], static_cast<uint32_t>(compressed.size()));
    StoreLe32(&out[12],
              compressed.empty() ? 0 : static_cast<uint32_t>(crc32(0, compressed.data(), static_cast<uInt>(size))));
    out.insert(out.end(), reserved.begin(), reserved.end());
    out.insert(out.end(), compressed.begin(), compressed.end());
    const std::string text = FormatTagArea(tags);
    out.insert(out.end(), text.begin(), text.end());

    return out;
}

void TestChunks()
{
    StreamingProgramBuilder b;
    b.AddFile("content/a.bin", std::vector<uint8_t>{9, 8, 7, 6, 5});
    b.AddSound(0x01000005);
    b.AddFile("code/b.bin", std::vector<uint8_t>{1});

    ProgramChunks chunks;
    const auto err = ParseStreamingProgram(b.Data(), chunks);

    XSFLIB_CHECK(b.Data().size() % 4 == 0);
    XSFLIB_CHECK(!err);
    XSFLIB_CHECK(chunks.files.size() == 2 && chunks.sound == 0x01000005u);
    XSFLIB_CHECK(chunks.files[0].path == "content/a.bin" &&
                 chunks.files[0].data == (std::vector<uint8_t>{9, 8, 7, 6, 5}));
    XSFLIB_CHECK(chunks.files[1].path == "code/b.bin" && chunks.files[1].data == std::vector<uint8_t>{1});

    // Unknown chunks are skipped.
    std::vector<uint8_t> prog = {'X', 'X', 'X', 'X', 4, 0, 0, 0, 1, 2, 3, 4};
    const std::vector<uint8_t> data = b.Data();
    prog.insert(prog.end(), data.begin(), data.end());
    ProgramChunks skipped;

    XSFLIB_CHECK(!ParseStreamingProgram(prog, skipped));
    XSFLIB_CHECK(skipped.files.size() == 2 && skipped.sound.has_value());

    // A truncated chunk is an error, even when its size is so large that adding it to the position wraps around on a
    // 32-bit host. A FILE chunk whose path runs past its end is an error too. So is an SND chunk without its word.
    const std::vector<uint8_t> kTruncated = {'F', 'I', 'L', 'E', 100, 0, 0, 0, 1, 2};
    const std::vector<uint8_t> kHuge = {'F', 'I', 'L', 'E', 0xf8, 0xff, 0xff, 0xff, 1, 2, 3, 4};
    const std::vector<uint8_t> kLongPath = {'F', 'I', 'L', 'E', 4, 0, 0, 0, 9, 0, 'a', 'b'};
    const std::vector<uint8_t> kShortSound = {'S', 'N', 'D', ' ', 2, 0, 0, 0, 1, 2, 0, 0};
    ProgramChunks bad;

    XSFLIB_CHECK(ParseStreamingProgram(kTruncated, bad).has_value());
    XSFLIB_CHECK(ParseStreamingProgram(kHuge, bad).has_value());
    XSFLIB_CHECK(ParseStreamingProgram(kLongPath, bad).has_value());
    XSFLIB_CHECK(ParseStreamingProgram(kShortSound, bad).has_value());
}

void TestProgramPieces()
{
    // Files of every length modulo four, copied and referred to, and large ones that compress well and badly.
    std::vector<std::vector<uint8_t>> files;
    std::mt19937 rng(5);
    for (std::size_t size : {std::size_t{0}, std::size_t{1}, std::size_t{2}, std::size_t{3}, std::size_t{4},
                             std::size_t{5}, std::size_t{3} << 20})
    {
        std::vector<uint8_t> file(size);
        for (uint8_t& byte : file)
        {
            byte = static_cast<uint8_t>(rng() % 4);
        }
        files.push_back(std::move(file));
    }

    std::vector<uint8_t> noise(1 << 20);
    for (uint8_t& byte : noise)
    {
        byte = static_cast<uint8_t>(rng());
    }
    files.push_back(std::move(noise));

    StreamingProgramBuilder copied;
    StreamingProgramBuilder referred;
    for (std::size_t i = 0; i < files.size(); ++i)
    {
        const std::string path = "content/" + std::string(i, 'f');
        copied.AddFile(path, files[i]);
        referred.AddFileRef(path, files[i]);
        if (i == 3)
        {
            copied.AddSound(0x01000003);
            referred.AddSound(0x01000003);
        }
    }

    const std::vector<uint8_t> program = copied.Data();

    XSFLIB_CHECK(referred.Data() == program);

    // A referred file is a piece of the program, not a copy.
    bool referred_in_place = false;
    for (const std::span<const uint8_t> piece : referred.Pieces())
    {
        referred_in_place = referred_in_place || piece.data() == files.back().data();
    }

    XSFLIB_CHECK(referred_in_place);

    // The pieces compress to the bytes that one call of compress2 gives.
    const std::vector<uint8_t> kReserved = {7, 7};
    const Tags kTags = {{"title", "pieces"}};

    const std::vector<uint8_t> file = WritePsf(kVersion, kReserved, program, kTags);

    XSFLIB_CHECK(file == OneCallPsf(kReserved, program, kTags));
    XSFLIB_CHECK(copied.WritePsf(kVersion, kReserved, kTags) == file);
    XSFLIB_CHECK(referred.WritePsf(kVersion, kReserved, kTags) == file);
    XSFLIB_CHECK(WritePsf(kVersion, {}, {}, {}) == OneCallPsf({}, {}, {}));
    XSFLIB_CHECK(StreamingProgramBuilder{}.WritePsf(kVersion, {}, kTags) == OneCallPsf({}, {}, kTags));

    ProgramChunks chunks;

    XSFLIB_CHECK(!ParsePsfChunks(file, chunks));
    XSFLIB_CHECK(chunks.files.size() == files.size() && chunks.sound == 0x01000003u);
    for (std::size_t i = 0; i < files.size() && i < chunks.files.size(); ++i)
    {
        XSFLIB_CHECK(chunks.files[i].path == "content/" + std::string(i, 'f') && chunks.files[i].data == files[i]);
    }
}

// The outcome of reading a program: its chunks, or the error.
struct ChunksRead
{
    std::string error;
    ProgramChunks chunks;
};

bool SameChunks(const ChunksRead& a, const ChunksRead& b)
{
    if (a.error != b.error || a.chunks.sound != b.chunks.sound || a.chunks.files.size() != b.chunks.files.size())
    {
        return false;
    }

    for (std::size_t i = 0; i < a.chunks.files.size(); ++i)
    {
        if (a.chunks.files[i].path != b.chunks.files[i].path || a.chunks.files[i].data != b.chunks.files[i].data)
        {
            return false;
        }
    }

    return true;
}

// A PSF file's chunks read both ways: ParsePsf and then ParseStreamingProgram, and ParsePsfChunks. ParsePsfChunks reads
// the chunks straight from the zlib stream. The two ways must give the same chunks and the same error for every file.
void CheckStreamedChunks(const std::vector<uint8_t>& file, const std::string& what)
{
    ChunksRead whole;
    PsfFile psf;
    if (const auto err = ParsePsf(file, psf))
    {
        whole.error = *err;
    }
    else if (const auto err2 = ParseStreamingProgram(psf.program, whole.chunks))
    {
        whole.error = *err2;
    }

    ChunksRead streamed;
    if (const auto err = ParsePsfChunks(file, streamed.chunks))
    {
        streamed.error = *err;
    }

    Check(SameChunks(whole, streamed),
          what + ": the streamed chunks match (\"" + whole.error + "\", \"" + streamed.error + "\")");
}

// A random program: FILE, SND and unknown chunks of random sizes, sometimes cut short or followed by stray bytes.
std::vector<uint8_t> RandomProgram(std::mt19937& rng)
{
    StreamingProgramBuilder b;
    const int chunks = static_cast<int>(rng() % 6);
    for (int i = 0; i < chunks; ++i)
    {
        std::vector<uint8_t> data(rng() % 3 == 0 ? rng() % 200000 : rng() % 40);
        for (uint8_t& byte : data)
        {
            byte = static_cast<uint8_t>(rng() % 7);
        }

        if (rng() % 4 == 0)
        {
            b.AddSound(static_cast<uint32_t>(rng()));
        }
        else
        {
            b.AddFile(std::string(rng() % 12, 'p'), data);
        }
    }

    std::vector<uint8_t> program = b.Data();
    if (rng() % 3 == 0)
    {
        program.insert(program.begin(), {'X', 'Y', 'Z', 'W', 5, 0, 0, 0, 1, 2, 3, 4, 5, 0, 0, 0});
    }
    if (rng() % 3 == 0 && !program.empty())
    {
        program.resize(rng() % program.size());
    }
    if (rng() % 4 == 0)
    {
        program.insert(program.end(), rng() % 10, 0xEE);
    }

    return program;
}

void TestStreamedChunks()
{
    StreamingProgramBuilder b;
    b.AddFile("content/a.bin", std::vector<uint8_t>(3 << 20, 0x5A));
    b.AddSound(0x01000005);
    b.AddFile("code/b.bin", std::vector<uint8_t>{1, 2, 3});
    const std::vector<uint8_t> program = b.Data();

    CheckStreamedChunks(WritePsf(kVersion, {9}, program, {}), "a set's chunks");
    CheckStreamedChunks(WritePsf(kVersion, {}, {}, {}), "no program");

    // The chunk errors, a program that stops in a chunk's padding, and one with stray bytes after its last chunk.
    const std::vector<std::vector<uint8_t>> kPrograms = {
        {'F', 'I', 'L', 'E', 100, 0, 0, 0, 1, 2},
        {'F', 'I', 'L', 'E', 0xf8, 0xff, 0xff, 0xff, 1, 2, 3, 4},
        {'F', 'I', 'L', 'E', 4, 0, 0, 0, 9, 0, 'a', 'b'},
        {'F', 'I', 'L', 'E', 1, 0, 0, 0, 9, 0, 0, 0},
        {'S', 'N', 'D', ' ', 2, 0, 0, 0, 1, 2, 0, 0},
        {'S', 'N', 'D', ' ', 5, 0, 0, 0, 1, 2, 3, 4, 5},
        {'S', 'N', 'D', ' ', 4, 0, 0, 0, 1, 2, 3, 4, 7, 7, 7},
    };

    for (std::size_t i = 0; i < kPrograms.size(); ++i)
    {
        CheckStreamedChunks(WritePsf(kVersion, {}, kPrograms[i], {}), "program " + std::to_string(i));
    }

    // The container's errors.
    const std::vector<uint8_t> file = WritePsf(kVersion, {}, program, {});
    std::vector<uint8_t> bad = file;
    bad[40] ^= 0x55;
    const uint32_t half = LoadLe32(&file[8]) / 2;
    std::vector<uint8_t> short_stream(file.begin(), file.begin() + 16 + half);
    StoreLe32(&short_stream[8], half);
    StoreLe32(&short_stream[12], static_cast<uint32_t>(crc32(0, &short_stream[16], half)));

    CheckStreamedChunks(bad, "a damaged program");
    CheckStreamedChunks({file.begin(), file.begin() + 20}, "a cut file");
    CheckStreamedChunks({'P', 'S'}, "not a PSF file");
    CheckStreamedChunks(short_stream, "a stream cut short");

    std::mt19937 rng(28);
    for (int round = 0; round < 300 && failures == 0; ++round)
    {
        CheckStreamedChunks(WritePsf(kVersion, {}, RandomProgram(rng), {}), "random program " + std::to_string(round));
    }
}

// A descriptor that is its magic and its version: one that a version 1 player rejects has another version.
template <uint32_t magic>
struct TestDescriptor
{
    static std::optional<TestDescriptor> Parse(const std::vector<uint8_t>& data)
    {
        if (data.size() < 8 || LoadLe32(data.data()) != magic)
        {
            return std::nullopt;
        }

        return TestDescriptor{LoadLe32(&data[4])};
    }

    std::optional<std::string> Check() const
    {
        if (version != 1)
        {
            return "the descriptor's version is " + std::to_string(version);
        }

        return std::nullopt;
    }

    std::vector<uint8_t> Serialize() const
    {
        std::vector<uint8_t> v;
        Put32(v, magic);
        Put32(v, version);

        return v;
    }

    uint32_t version = 1;
};

constexpr uint32_t kProcessMagic = 0x50525453; // "STRP"
constexpr uint32_t kArchiveMagic = 0x41525453; // "STRA"

// A streaming format with both modes.
struct BothModes
{
    using ProcessDescriptor = TestDescriptor<kProcessMagic>;
    using ArchiveDescriptor = TestDescriptor<kArchiveMagic>;

    static constexpr uint8_t kPsfVersion = kVersion;
    static constexpr const char* kName = "STR";
    static constexpr bool kStreaming = true;
};

// A streaming format with archive mode only. Its files take the article "an".
struct ArchiveOnly
{
    using ProcessDescriptor = void;
    using ArchiveDescriptor = TestDescriptor<kArchiveMagic>;

    static constexpr uint8_t kPsfVersion = kVersion;
    static constexpr const char* kFile = "an STR file";
    static constexpr bool kStreaming = true;
};

// A set of a streaming format loads its chunks in loading order and the last descriptor. Every set selects a sound.
void TestStreamingSets()
{
    std::map<std::string, std::vector<uint8_t>> files;
    {
        StreamingProgramBuilder b;
        b.AddFile("f", std::vector<uint8_t>{1});
        files["lib"] = WritePsf(kVersion, TestDescriptor<kArchiveMagic>{}.Serialize(), b.Data(), {{"game", "G"}});
    }
    {
        StreamingProgramBuilder b;
        b.AddFile("f", std::vector<uint8_t>{2});
        b.AddSound(7);
        files["mini"] = WritePsf(kVersion, {}, b.Data(), {{"_lib", "lib"}, {"title", "T"}});
    }
    LoadedSet<BothModes> both;

    const auto err = LoadSet("mini", MapReader(files), both);

    XSFLIB_CHECK(!err);
    XSFLIB_CHECK(both.archive && !both.descriptor && both.sound == 7u && both.tags.at("title") == "T");
    XSFLIB_CHECK(both.files.size() == 2 && both.File("f") && *both.File("f") == std::vector<uint8_t>{2});
    XSFLIB_CHECK(both.sources == (std::vector<std::string>{"lib", "mini"}));

    // A non-const File gives the data to move out.
    std::vector<uint8_t>* data = both.File("f");

    XSFLIB_CHECK(data && (*data)[0] == 2);

    LoadedSet<ArchiveOnly> archive_only;

    XSFLIB_CHECK(!LoadSet("mini", MapReader(files), archive_only) && archive_only.sound == 7u);

    // A game-mode set of a streaming format selects a sound too.
    {
        StreamingProgramBuilder b;
        b.AddFile("g", std::vector<uint8_t>{3});
        files["game"] = WritePsf(kVersion, TestDescriptor<kProcessMagic>{}.Serialize(), b.Data(), {});
    }

    XSFLIB_CHECK(LoadError<BothModes>("game", MapReader(files)) ==
                 "no sound selected (SND chunk) in game or its libraries");

    // The messages that differ between the formats.
    files["plain"] = WritePsf(kVersion, {}, {}, {});
    files["other"] = WritePsf(0x11, {}, {}, {});

    XSFLIB_CHECK(LoadError<BothModes>("plain", MapReader(files)) ==
                 "no process or archive descriptor in plain or its libraries");
    XSFLIB_CHECK(LoadError<ArchiveOnly>("plain", MapReader(files)) ==
                 "no archive descriptor in plain or its libraries");
    XSFLIB_CHECK(LoadError<BothModes>("other", MapReader(files)) == "other: not a STR file (PSF version 0x11)");
    XSFLIB_CHECK(LoadError<ArchiveOnly>("other", MapReader(files)) == "other: not an STR file (PSF version 0x11)");

    // The program is read after the _lib, straight into its chunks: a library's damage shows first.
    std::vector<uint8_t> damaged = files["mini"];
    damaged[20] ^= 0x55;
    files["damaged"] = damaged;
    files["after"] = WritePsf(kVersion, {}, {}, {{"_lib", "damaged"}});

    XSFLIB_CHECK(LoadError<BothModes>("after", MapReader(files)) == "damaged: program CRC mismatch");
}

} // namespace

int main()
{
    TestChunks();
    TestProgramPieces();
    TestStreamedChunks();
    TestStreamingSets();

    return xsflib::testing::Summary("psf streaming");
}
