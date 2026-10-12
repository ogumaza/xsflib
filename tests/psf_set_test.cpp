// SPDX-License-Identifier: MIT

// Unit tests for the programs and sets of the formats that copy a set's files into their programs (xsflib/psf/program.h
// and set.h): chunks, the driver block's checks, _lib chains and the loader's messages, with a test format that has
// archive mode and one that hasn't.

#include <cstdint>
#include <cstdio>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "xsflib/bytes.h"
#include "xsflib/psf/program.h"
#include "xsflib/psf/psf.h"
#include "xsflib/psf/set.h"
#include "xsflib/testing/check.h"
#include "xsflib/testing/sets.h"

using namespace xsflib::psf;
using xsflib::LoadLe32;
using xsflib::testing::failures;

namespace
{

// A descriptor that a reserved area holds when it starts with `magic`. Its second word is its version. A version 1
// player accepts version 1 only.
template <uint32_t magic>
struct TestDescriptor
{
    static std::optional<TestDescriptor> Parse(const std::vector<uint8_t>& data)
    {
        if (data.size() < 8 || LoadLe32(data.data()) != magic)
        {
            return std::nullopt;
        }

        return TestDescriptor{LoadLe32(data.data() + 4)};
    }

    std::optional<std::string> Check() const
    {
        if (version != 1)
        {
            return "descriptor version " + std::to_string(version);
        }

        return std::nullopt;
    }

    uint32_t version = 1;
};

std::vector<uint8_t> Reserved(uint32_t magic, uint32_t version = 1)
{
    std::vector<uint8_t> v;
    Put32(v, magic);
    Put32(v, version);

    return v;
}

constexpr uint32_t kProcessMagic = 0x50545354; // "TSTP"
constexpr uint32_t kArchiveMagic = 0x41545354; // "TSTA"
constexpr uint32_t kDriverMagic = 0x44545354;  // "TSTD"
constexpr uint32_t kDriverAddress = 0x1000;

// A format with both modes and a little-endian driver block with an output mode.
struct BothModes
{
    using ProcessDescriptor = TestDescriptor<kProcessMagic>;
    using ArchiveDescriptor = TestDescriptor<kArchiveMagic>;

    static constexpr uint8_t kPsfVersion = 0x7e;
    static constexpr const char* kName = "TST";
    static constexpr DriverBlockLayout kDriverBlock{.address = kDriverAddress,
                                                    .magic = kDriverMagic,
                                                    .version = 1,
                                                    .version_offset = 4,
                                                    .big_endian = false,
                                                    .output_mode_offset = 0x10,
                                                    .output_mode = 1};
};

// A format with game mode only and a big-endian driver block.
struct GameModeOnly
{
    using ProcessDescriptor = TestDescriptor<kProcessMagic>;
    using ArchiveDescriptor = void;

    static constexpr uint8_t kPsfVersion = 0x7f;
    static constexpr const char* kName = "GMO";
    static constexpr DriverBlockLayout kDriverBlock{
        .address = kDriverAddress, .magic = kDriverMagic, .big_endian = true};
};

// Files in memory, read by path.
struct Files
{
    FileReader Reader() const
    {
        return [this](const std::string& path, std::vector<uint8_t>& data)
        {
            const auto it = files.find(path);
            if (it == files.end())
            {
                return false;
            }

            data = it->second;

            return true;
        };
    }

    std::map<std::string, std::vector<uint8_t>> files;
};

std::vector<uint8_t> Word(uint32_t value, bool big_endian)
{
    return big_endian
               ? std::vector<uint8_t>{uint8_t(value >> 24), uint8_t(value >> 16), uint8_t(value >> 8), uint8_t(value)}
               : std::vector<uint8_t>{uint8_t(value), uint8_t(value >> 8), uint8_t(value >> 16), uint8_t(value >> 24)};
}

void TestChunks()
{
    ProgramBuilder b;
    b.AddMemory(0x100, std::vector<uint8_t>{1, 2, 3});
    b.AddFile("a/b.bin", std::vector<uint8_t>{9, 8, 7, 6, 5});
    b.AddSound(0x01000007);
    ProgramChunks c;

    XSFLIB_CHECK(b.Data().size() % 4 == 0);
    XSFLIB_CHECK(!ParseProgram(b.Data(), c));
    XSFLIB_CHECK(c.memory.size() == 1 && c.memory[0].address == 0x100 &&
                 c.memory[0].data == std::vector<uint8_t>({1, 2, 3}));
    XSFLIB_CHECK(c.files.size() == 1 && c.files[0].path == "a/b.bin" &&
                 c.files[0].data == std::vector<uint8_t>({9, 8, 7, 6, 5}));
    XSFLIB_CHECK(c.sound == 0x01000007u);

    // Without SND chunks, the chunk is skipped like an unknown one.
    ProgramChunks skipped;

    XSFLIB_CHECK(!ParseProgram(b.Data(), skipped, ProgramOptions{.sound_chunks = false}));
    XSFLIB_CHECK(!skipped.sound && skipped.files.size() == 1);

    // A chunk of an unknown type is skipped.
    std::vector<uint8_t> unknown = {'X', 'X', 'X', 'X', 4, 0, 0, 0, 1, 2, 3, 4};
    unknown.insert(unknown.end(), b.Data().begin(), b.Data().end());
    ProgramChunks after_unknown;

    XSFLIB_CHECK(!ParseProgram(unknown, after_unknown));
    XSFLIB_CHECK(after_unknown.memory.size() == 1 && after_unknown.files.size() == 1 && after_unknown.sound);

    // Damaged chunks.
    const auto error = [](std::vector<uint8_t> program, bool sound_chunks = true)
    {
        ProgramChunks out;
        return ParseProgram(program, out, ProgramOptions{.sound_chunks = sound_chunks}).value_or("");
    };

    std::vector<uint8_t> truncated = b.Data();
    truncated.resize(truncated.size() - 4);

    XSFLIB_CHECK(error(truncated) == "truncated chunk");
    // A size so large that adding it to the position would wrap around on a 32-bit host.
    XSFLIB_CHECK(error({'M', 'E', 'M', ' ', 0xf8, 0xff, 0xff, 0xff, 1, 2, 3, 4}) == "truncated chunk");
    XSFLIB_CHECK(error({'M', 'E', 'M', ' ', 2, 0, 0, 0, 1, 2, 0, 0}) == "bad MEM chunk");
    XSFLIB_CHECK(error({'F', 'I', 'L', 'E', 1, 0, 0, 0, 1, 0, 0, 0}) == "bad FILE chunk");
    XSFLIB_CHECK(error({'F', 'I', 'L', 'E', 4, 0, 0, 0, 9, 0, 'a', 'b'}) == "bad FILE chunk");
    XSFLIB_CHECK(error({'S', 'N', 'D', ' ', 2, 0, 0, 0, 1, 2, 0, 0}) == "bad SND chunk");
    XSFLIB_CHECK(error({'S', 'N', 'D', ' ', 2, 0, 0, 0, 1, 2, 0, 0}, false).empty());

    // A later FILE chunk with the same path replaces an earlier one.
    const std::vector<FileChunk> kFiles = {{"x", {1}}, {"y", {2}}, {"x", {3}}};

    XSFLIB_CHECK(FindFile(kFiles, "x") && *FindFile(kFiles, "x") == std::vector<uint8_t>({3}));
    XSFLIB_CHECK(FindFile(kFiles, "z") == nullptr);
}

void TestLoadedMemory()
{
    // A later MEM chunk overwrites an earlier one.
    const std::vector<MemChunk> kMemory = {{0x10, {1, 2, 3, 4, 5}}, {0x12, {0xaa}}};

    XSFLIB_CHECK(LoadedByte(kMemory, 0x12) == 0xaa);
    XSFLIB_CHECK(LoadedByte(kMemory, 0x14) == 5);
    XSFLIB_CHECK(!LoadedByte(kMemory, 0x15));
    XSFLIB_CHECK(LoadedWord(kMemory, 0x10, false) == 0x04aa0201u);
    XSFLIB_CHECK(LoadedWord(kMemory, 0x10, true) == 0x0102aa04u);
    XSFLIB_CHECK(!LoadedWord(kMemory, 0x12, false));
}

void TestDriverBlock()
{
    const DriverBlockLayout& layout = BothModes::kDriverBlock;
    const auto block = [](uint32_t version, std::optional<uint32_t> mode, bool big_endian)
    {
        std::vector<uint8_t> data = Word(kDriverMagic, big_endian);
        const std::vector<uint8_t> v = Word(version, big_endian);
        data.insert(data.end(), v.begin(), v.end());
        if (mode)
        {
            data.resize(0x10);
            const std::vector<uint8_t> m = Word(*mode, big_endian);
            data.insert(data.end(), m.begin(), m.end());
        }

        return std::vector<MemChunk>{{kDriverAddress, data}};
    };

    XSFLIB_CHECK(!CheckDriverBlock({}, layout));
    XSFLIB_CHECK(!CheckDriverBlock(block(1, 1, false), layout));
    XSFLIB_CHECK(CheckDriverBlock(block(1, std::nullopt, false), layout) == "the driver block is cut short");
    XSFLIB_CHECK(CheckDriverBlock(block(2, 1, false), layout) ==
                 "driver block version 2; this player supports version 1");
    XSFLIB_CHECK(CheckDriverBlock(block(1, 2, false), layout) ==
                 std::string("the driver block's output mode is 2") + OutputModeExpected());

    // A big-endian block without an output mode.
    XSFLIB_CHECK(!CheckDriverBlock(block(1, std::nullopt, true), GameModeOnly::kDriverBlock));
    XSFLIB_CHECK(CheckDriverBlock(block(3, std::nullopt, true), GameModeOnly::kDriverBlock) ==
                 "driver block version 3; this player supports version 1");
    XSFLIB_CHECK(!CheckDriverBlock(block(1, std::nullopt, false), GameModeOnly::kDriverBlock));

    // A format whose drivers have block versions 1 and 2.
    constexpr DriverBlockLayout kTwoVersions{
        .address = kDriverAddress, .magic = kDriverMagic, .newest_version = 2, .big_endian = true};

    XSFLIB_CHECK(!CheckDriverBlock(block(1, std::nullopt, true), kTwoVersions));
    XSFLIB_CHECK(!CheckDriverBlock(block(2, std::nullopt, true), kTwoVersions));
    XSFLIB_CHECK(CheckDriverBlock(block(3, std::nullopt, true), kTwoVersions) ==
                 "driver block version 3; this player supports versions 1 to 2");
    XSFLIB_CHECK(CheckDriverBlock(block(0, std::nullopt, true), kTwoVersions) ==
                 "driver block version 0; this player supports versions 1 to 2");
}

template <typename Format>
std::vector<uint8_t> File(const std::vector<uint8_t>& reserved, const ProgramBuilder& program, const Tags& tags)
{
    return WritePsf(Format::kPsfVersion, reserved, program.Data(), tags);
}

void TestLoadSet()
{
    // A mini file and its libraries: _lib first, then the file itself, then _lib2.
    Files f;
    ProgramBuilder lib;
    lib.AddFile("archive", {1, 2, 3});
    lib.AddMemory(0x20, std::vector<uint8_t>{7});
    ProgramBuilder lib2;
    lib2.AddFile("archive", {4});
    lib2.AddMemory(0x22, std::vector<uint8_t>{9});
    ProgramBuilder mini;
    mini.AddSound(5);
    mini.AddMemory(0x21, std::vector<uint8_t>{8});
    f.files["dir/lib.tst"] = File<BothModes>(Reserved(kArchiveMagic), lib, {{"game", "Lib"}});
    f.files["dir/sub/lib2.tst"] = File<BothModes>({}, lib2, {});
    f.files["dir/song.minitst"] =
        File<BothModes>({}, mini, {{"_lib", "lib.tst"}, {"_lib2", "sub/lib2.tst"}, {"title", "Song"}});
    LoadedSet<BothModes> set;

    XSFLIB_CHECK(!LoadSet("dir/song.minitst", f.Reader(), set));
    XSFLIB_CHECK(set.archive && !set.descriptor && set.sound == 5u);
    XSFLIB_CHECK(set.sources == std::vector<std::string>({"dir/lib.tst", "dir/song.minitst", "dir/sub/lib2.tst"}));
    XSFLIB_CHECK(set.File("archive") && *set.File("archive") == std::vector<uint8_t>({4}));
    // The memory comes in the files' loading order. The set takes its tags from the mini file alone.
    XSFLIB_CHECK(set.memory.size() == 3 && set.memory[0].data[0] == 7 && set.memory[1].data[0] == 8 &&
                 set.memory[2].data[0] == 9);
    XSFLIB_CHECK(set.tags["title"] == "Song" && !set.tags.count("game") && !set.tags.count("unknown"));

    // The sets that the loader rejects.
    f.files["dir/orphan.minitst"] = File<BothModes>({}, mini, {{"_lib", "missing.tst"}});
    f.files["other.tst"] = File<GameModeOnly>(Reserved(kProcessMagic), ProgramBuilder(), {});
    f.files["nothing.tst"] = File<BothModes>({}, ProgramBuilder(), {});
    f.files["nosound.tst"] = File<BothModes>(Reserved(kArchiveMagic), ProgramBuilder(), {});
    f.files["v2.tst"] = File<BothModes>(Reserved(kProcessMagic, 2), ProgramBuilder(), {});
    f.files["loop.tst"] = File<BothModes>(Reserved(kProcessMagic), ProgramBuilder(), {{"_lib", "loop.tst"}});
    const auto error = [&f](const std::string& path)
    {
        return xsflib::testing::LoadError<BothModes>(path, f.Reader());
    };

    XSFLIB_CHECK(error("missing.tst") == "can't read missing.tst");
    XSFLIB_CHECK(error("dir/orphan.minitst") == "can't read dir/missing.tst");
    XSFLIB_CHECK(error("other.tst") == "other.tst: not a TST file (PSF version 0x7f)");
    XSFLIB_CHECK(error("nothing.tst") == "no process or archive descriptor in nothing.tst or its libraries");
    XSFLIB_CHECK(error("nosound.tst") == "no sound selected (SND chunk) in nosound.tst or its libraries");
    XSFLIB_CHECK(error("v2.tst") == "v2.tst: descriptor version 2");
    XSFLIB_CHECK(error("loop.tst") == "_lib nesting too deep");

    // A path up from the mini file's folder reaches the reader as written.
    f.files["dir/sub/../lib.tst"] = f.files["dir/lib.tst"];
    f.files["dir/sub/up.minitst"] = File<BothModes>({}, mini, {{"_lib", "../lib.tst"}});

    XSFLIB_CHECK(!LoadSet("dir/sub/up.minitst", f.Reader(), set));
    XSFLIB_CHECK(set.sources == std::vector<std::string>({"dir/sub/../lib.tst", "dir/sub/up.minitst"}));

    // An empty _lib2 names no library, like an empty _lib. _lib3 still loads.
    f.files["dir/gap.minitst"] =
        File<BothModes>({}, mini, {{"_lib", "lib.tst"}, {"_lib2", ""}, {"_lib3", "sub/lib2.tst"}});

    XSFLIB_CHECK(!LoadSet("dir/gap.minitst", f.Reader(), set));
    XSFLIB_CHECK(set.sources == std::vector<std::string>({"dir/lib.tst", "dir/gap.minitst", "dir/sub/lib2.tst"}));

    // A set that names more files than a set can have. Each of these names the next one ten times. Loading them all
    // would take 10^10 loads.
    for (int level = 0; level < 10; level++)
    {
        Tags tags;
        for (int n = 2; n <= 11; n++)
        {
            tags["_lib" + std::to_string(n)] = "f" + std::to_string(level + 1) + ".tst";
        }

        f.files["f" + std::to_string(level) + ".tst"] = File<BothModes>({}, ProgramBuilder(), tags);
    }

    f.files["f10.tst"] = File<BothModes>({}, ProgramBuilder(), {});

    XSFLIB_CHECK(error("f0.tst") == "more than 64 files in the set");

    // The game-mode set's driver block is checked.
    ProgramBuilder game;
    std::vector<uint8_t> driver = Word(kDriverMagic, false);
    const std::vector<uint8_t> version = Word(2, false);
    driver.insert(driver.end(), version.begin(), version.end());
    game.AddMemory(kDriverAddress, driver);
    f.files["game.tst"] = File<BothModes>(Reserved(kProcessMagic), game, {});

    XSFLIB_CHECK(error("game.tst") == "game.tst: the driver block is cut short");

    // A mini file's memory counts too: a set plays a library's good driver block as a mini file patches it.
    ProgramBuilder good;
    std::vector<uint8_t> block = Word(kDriverMagic, false);
    const std::vector<uint8_t> one = Word(1, false);
    block.insert(block.end(), one.begin(), one.end());
    block.resize(0x10);
    block.insert(block.end(), one.begin(), one.end());
    good.AddMemory(kDriverAddress, block);
    f.files["good.tst"] = File<BothModes>(Reserved(kProcessMagic), good, {});

    ProgramBuilder patch;
    patch.AddMemory(kDriverAddress + 4, Word(2, false));
    f.files["patched.minitst"] = File<BothModes>({}, patch, {{"_lib", "good.tst"}});

    XSFLIB_CHECK(error("good.tst").empty());
    XSFLIB_CHECK(error("patched.minitst") == "patched.minitst: driver block version 2; this player supports version 1");

    // A file in an archive, by foobar2000's path for it: unpack://zip|<length>|<the archive's path>|<its path in the
    // archive>. With | as a separator too, the library is found in the archive. Without it, the library is looked for
    // beside the archive.
    const std::string kZip = "unpack://zip|23|file://C:\\music\\set.zip|";
    f.files[kZip + "set.tst"] = f.files["dir/lib.tst"];
    f.files[kZip + "x.minitst"] = File<BothModes>({}, mini, {{"_lib", "set.tst"}});

    XSFLIB_CHECK(!LoadSet(kZip + "x.minitst", f.Reader(), set, "/\\|"));
    XSFLIB_CHECK(set.sources == std::vector<std::string>({kZip + "set.tst", kZip + "x.minitst"}));
    XSFLIB_CHECK(error(kZip + "x.minitst") == "can't read unpack://zip|23|file://C:\\music\\set.tst");

    // A format without archive mode reads no archive descriptor and skips SND chunks.
    Files g;
    ProgramBuilder with_sound;
    with_sound.AddSound(9);
    g.files["a.gmo"] = File<GameModeOnly>(Reserved(kArchiveMagic), with_sound, {});
    g.files["b.gmo"] = File<GameModeOnly>(Reserved(kProcessMagic), with_sound, {});
    LoadedSet<GameModeOnly> game_set;

    XSFLIB_CHECK(LoadSet("a.gmo", g.Reader(), game_set) == "no process descriptor in a.gmo or its libraries");
    XSFLIB_CHECK(!LoadSet("b.gmo", g.Reader(), game_set));
    XSFLIB_CHECK(game_set.descriptor && !game_set.sound);
}

void TestTagReaders()
{
    const Tags kTags = {{"length", "1:02.5"}, {"fade", "bad"}, {"volume", "0.5dB"}};

    XSFLIB_CHECK(TagTimeMs(kTags, "length") == 62500);
    XSFLIB_CHECK(TagTimeMs(kTags, "fade") == -1);
    XSFLIB_CHECK(TagTimeMs(kTags, "missing") == -1);
    XSFLIB_CHECK(TagVolume(kTags) == 0.5);
    XSFLIB_CHECK(TagVolume({}) == 1.0);
    XSFLIB_CHECK(TagVolume({{"volume", "1e999"}}) == 1.0);
    XSFLIB_CHECK(TagVolume({{"volume", "x"}}) == 1.0);
}

} // namespace

int main()
{
    TestChunks();
    TestLoadedMemory();
    TestDriverBlock();
    TestLoadSet();
    TestTagReaders();

    if (failures)
    {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }

    std::printf("psf set tests passed\n");

    return 0;
}
