// SPDX-License-Identifier: MIT

// Unit tests for the programs and sets of the formats that copy a set's files into their programs (xsflib/psf/program.h
// and set.h): chunks, the driver block's checks, _lib chains and the loader's messages, with a test format that has
// archive mode and one that hasn't. Also the walk over a set's files that the loader runs (WalkSet).

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "test_descriptor.h"
#include "xsflib/psf/program.h"
#include "xsflib/psf/psf.h"
#include "xsflib/psf/set.h"
#include "xsflib/testing/check.h"
#include "xsflib/testing/sets.h"

using namespace xsflib::psf;
using xsflib::testing::ExactReader;

namespace
{

// The reserved area of a file with the descriptor of `magic` and `version`.
template <uint32_t magic>
std::vector<uint8_t> Reserved(uint32_t version = 1)
{
    return TestDescriptor<magic>{version}.Serialize();
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

// A set's files by path.
using SetFiles = std::map<std::string, std::vector<uint8_t>>;

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

void TestSoundLists()
{
    // A SND chunk with a list selects its sounds in its order. The first one is the program's sound.
    ProgramBuilder b;
    b.AddSounds(std::vector<uint32_t>{7, 0x01000003, 9});
    ProgramChunks c;

    XSFLIB_CHECK(!ParseProgram(b.Data(), c));
    XSFLIB_CHECK(c.sound == 7u && c.sounds == std::vector<uint32_t>({7, 0x01000003, 9}));

    // A list of one sound is AddSound's chunk, and a program with it reads as before.
    ProgramBuilder one;
    one.AddSound(5);
    ProgramBuilder list_of_one;
    list_of_one.AddSounds(std::vector<uint32_t>{5});
    ProgramChunks single;

    XSFLIB_CHECK(one.Data() == list_of_one.Data());
    XSFLIB_CHECK(one.Data() == std::vector<uint8_t>({'S', 'N', 'D', ' ', 4, 0, 0, 0, 5, 0, 0, 0}));
    XSFLIB_CHECK(!ParseProgram(one.Data(), single));
    XSFLIB_CHECK(single.sound == 5u && single.sounds == std::vector<uint32_t>({5}));

    // A later SND chunk replaces the whole list, a shorter list too.
    b.AddSound(2);
    ProgramChunks replaced;

    XSFLIB_CHECK(!ParseProgram(b.Data(), replaced));
    XSFLIB_CHECK(replaced.sound == 2u && replaced.sounds == std::vector<uint32_t>({2}));

    // A partial word at the chunk's end is skipped.
    const std::vector<uint8_t> kPartial = {'S', 'N', 'D', ' ', 6, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0};
    ProgramChunks partial;

    XSFLIB_CHECK(!ParseProgram(kPartial, partial));
    XSFLIB_CHECK(partial.sound == 1u && partial.sounds == std::vector<uint32_t>({1}));

    // Without SND chunks, the list stays empty.
    ProgramChunks skipped;

    XSFLIB_CHECK(!ParseProgram(b.Data(), skipped, ProgramOptions{.sound_chunks = false}));
    XSFLIB_CHECK(!skipped.sound && skipped.sounds.empty());

    // A chunk selects at least one sound.
    const auto add_none = []
    {
        ProgramBuilder empty;
        empty.AddSounds({});
    };

    XSFLIB_CHECK(!xsflib::testing::ErrorOf<std::invalid_argument>(add_none).empty());
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

// The bytes of a driver block of the test formats: the magic and `version`, then `mode` at offset 0x10 if given.
std::vector<uint8_t> DriverBlockBytes(uint32_t version, std::optional<uint32_t> mode, bool big_endian)
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

    return data;
}

// The memory of a set that holds only a driver block (DriverBlockBytes).
std::vector<MemChunk> DriverBlock(uint32_t version, std::optional<uint32_t> mode, bool big_endian)
{
    return {{kDriverAddress, DriverBlockBytes(version, mode, big_endian)}};
}

void TestDriverBlock()
{
    const DriverBlockLayout& layout = BothModes::kDriverBlock;

    XSFLIB_CHECK(!CheckDriverBlock({}, layout));
    XSFLIB_CHECK(!CheckDriverBlock(DriverBlock(1, 1, false), layout));
    XSFLIB_CHECK(CheckDriverBlock(DriverBlock(1, std::nullopt, false), layout) == "the driver block is cut short");
    XSFLIB_CHECK(CheckDriverBlock(DriverBlock(2, 1, false), layout) ==
                 "driver block version 2; this player supports version 1");
    XSFLIB_CHECK(CheckDriverBlock(DriverBlock(1, 2, false), layout) ==
                 "the driver block's output mode is 2; version 1 supports only stereo (1)");

    // A big-endian block without an output mode.
    XSFLIB_CHECK(!CheckDriverBlock(DriverBlock(1, std::nullopt, true), GameModeOnly::kDriverBlock));
    XSFLIB_CHECK(CheckDriverBlock(DriverBlock(3, std::nullopt, true), GameModeOnly::kDriverBlock) ==
                 "driver block version 3; this player supports version 1");
    XSFLIB_CHECK(!CheckDriverBlock(DriverBlock(1, std::nullopt, false), GameModeOnly::kDriverBlock));

    // A format whose drivers have block versions 1 and 2.
    constexpr DriverBlockLayout kTwoVersions{
        .address = kDriverAddress, .magic = kDriverMagic, .newest_version = 2, .big_endian = true};

    XSFLIB_CHECK(!CheckDriverBlock(DriverBlock(1, std::nullopt, true), kTwoVersions));
    XSFLIB_CHECK(!CheckDriverBlock(DriverBlock(2, std::nullopt, true), kTwoVersions));
    XSFLIB_CHECK(CheckDriverBlock(DriverBlock(3, std::nullopt, true), kTwoVersions) ==
                 "driver block version 3; this player supports versions 1 to 2");
    XSFLIB_CHECK(CheckDriverBlock(DriverBlock(0, std::nullopt, true), kTwoVersions) ==
                 "driver block version 0; this player supports versions 1 to 2");
}

template <typename Format>
std::vector<uint8_t> File(const std::vector<uint8_t>& reserved, const ProgramBuilder& program, const Tags& tags)
{
    return WritePsf(Format::kPsfVersion, reserved, program.Data(), tags);
}

// The loader's error for the set of BothModes at `path`, or an empty string if the set loads.
std::string LoadError(const SetFiles& files, const std::string& path)
{
    return xsflib::testing::LoadError<BothModes>(path, ExactReader(files));
}

// A mini file's program: sound 5, and a byte at 0x21.
ProgramBuilder MiniProgram()
{
    ProgramBuilder mini;
    mini.AddSound(5);
    mini.AddMemory(0x21, std::vector<uint8_t>{8});

    return mini;
}

// Two libraries that both hold the file "archive". dir/lib.tst has archive mode's descriptor, the tag "game" and a
// byte at 0x20. dir/sub/lib2.tst has a byte at 0x22.
SetFiles Libraries()
{
    ProgramBuilder lib;
    lib.AddFile("archive", {1, 2, 3});
    lib.AddMemory(0x20, std::vector<uint8_t>{7});
    ProgramBuilder lib2;
    lib2.AddFile("archive", {4});
    lib2.AddMemory(0x22, std::vector<uint8_t>{9});

    return {{"dir/lib.tst", File<BothModes>(Reserved<kArchiveMagic>(), lib, {{"game", "Lib"}})},
            {"dir/sub/lib2.tst", File<BothModes>({}, lib2, {})}};
}

// A mini file and its libraries: _lib first, then the file itself, then _lib2.
void TestLoadSet()
{
    SetFiles files = Libraries();
    files["dir/song.minitst"] =
        File<BothModes>({}, MiniProgram(), {{"_lib", "lib.tst"}, {"_lib2", "sub/lib2.tst"}, {"title", "Song"}});
    LoadedSet<BothModes> set;

    XSFLIB_CHECK(!LoadSet("dir/song.minitst", ExactReader(files), set));
    XSFLIB_CHECK(set.archive && !set.descriptor && set.sound == 5u && set.sounds == std::vector<uint32_t>({5}));
    XSFLIB_CHECK(set.sources == std::vector<std::string>({"dir/lib.tst", "dir/song.minitst", "dir/sub/lib2.tst"}));
    XSFLIB_CHECK(set.File("archive") && *set.File("archive") == std::vector<uint8_t>({4}));
    // The memory comes in the files' loading order. The set takes its tags from the mini file alone.
    XSFLIB_CHECK(set.memory.size() == 3 && set.memory[0].data[0] == 7 && set.memory[1].data[0] == 8 &&
                 set.memory[2].data[0] == 9);
    XSFLIB_CHECK(set.tags["title"] == "Song" && !set.tags.count("game") && !set.tags.count("unknown"));
}

// A file that selects sounds replaces the whole list of the files loaded before it. A mini file without a SND chunk
// plays its library's list.
void TestSoundListsOfSets()
{
    ProgramBuilder listing_lib;
    listing_lib.AddFile("archive", {1});
    listing_lib.AddSounds(std::vector<uint32_t>{1, 2});
    ProgramBuilder listing_mini;
    listing_mini.AddSounds(std::vector<uint32_t>{6, 5, 4});
    const SetFiles kFiles = {
        {"list/lib.tst", File<BothModes>(Reserved<kArchiveMagic>(), listing_lib, {})},
        {"list/three.minitst", File<BothModes>({}, listing_mini, {{"_lib", "lib.tst"}})},
        {"list/none.minitst", File<BothModes>({}, ProgramBuilder(), {{"_lib", "lib.tst"}})},
    };
    LoadedSet<BothModes> three;
    LoadedSet<BothModes> none;

    XSFLIB_CHECK(!LoadSet("list/three.minitst", ExactReader(kFiles), three));
    XSFLIB_CHECK(!LoadSet("list/none.minitst", ExactReader(kFiles), none));

    XSFLIB_CHECK(three.sound == 6u && three.sounds == std::vector<uint32_t>({6, 5, 4}));
    XSFLIB_CHECK(none.sound == 1u && none.sounds == std::vector<uint32_t>({1, 2}));
}

// The sets that the loader rejects.
void TestRejectedSets()
{
    SetFiles files = Libraries();
    files["dir/orphan.minitst"] = File<BothModes>({}, MiniProgram(), {{"_lib", "missing.tst"}});
    files["other.tst"] = File<GameModeOnly>(Reserved<kProcessMagic>(), ProgramBuilder(), {});
    files["nothing.tst"] = File<BothModes>({}, ProgramBuilder(), {});
    files["nosound.tst"] = File<BothModes>(Reserved<kArchiveMagic>(), ProgramBuilder(), {});
    files["v2.tst"] = File<BothModes>(Reserved<kProcessMagic>(2), ProgramBuilder(), {});
    files["loop.tst"] = File<BothModes>(Reserved<kProcessMagic>(), ProgramBuilder(), {{"_lib", "loop.tst"}});

    XSFLIB_CHECK(LoadError(files, "missing.tst") == "can't read missing.tst");
    XSFLIB_CHECK(LoadError(files, "dir/orphan.minitst") == "can't read dir/missing.tst");
    XSFLIB_CHECK(LoadError(files, "other.tst") == "other.tst: not a TST file (PSF version 0x7f)");
    XSFLIB_CHECK(LoadError(files, "nothing.tst") == "no process or archive descriptor in nothing.tst or its libraries");
    XSFLIB_CHECK(LoadError(files, "nosound.tst") == "no sound selected (SND chunk) in nosound.tst or its libraries");
    XSFLIB_CHECK(LoadError(files, "v2.tst") == "v2.tst: descriptor version 2");
    XSFLIB_CHECK(LoadError(files, "loop.tst") == "_lib nesting too deep");
}

// A path up from the mini file's folder reaches the reader as written. An empty _lib2 names no library, like an empty
// _lib. _lib3 still loads.
void TestLibraryPaths()
{
    SetFiles files = Libraries();
    files["dir/sub/../lib.tst"] = files["dir/lib.tst"];
    files["dir/sub/up.minitst"] = File<BothModes>({}, MiniProgram(), {{"_lib", "../lib.tst"}});
    files["dir/gap.minitst"] =
        File<BothModes>({}, MiniProgram(), {{"_lib", "lib.tst"}, {"_lib2", ""}, {"_lib3", "sub/lib2.tst"}});
    LoadedSet<BothModes> up;
    LoadedSet<BothModes> gap;

    XSFLIB_CHECK(!LoadSet("dir/sub/up.minitst", ExactReader(files), up));
    XSFLIB_CHECK(!LoadSet("dir/gap.minitst", ExactReader(files), gap));

    XSFLIB_CHECK(up.sources == std::vector<std::string>({"dir/sub/../lib.tst", "dir/sub/up.minitst"}));
    XSFLIB_CHECK(gap.sources == std::vector<std::string>({"dir/lib.tst", "dir/gap.minitst", "dir/sub/lib2.tst"}));
}

// A set that names more files than a set can have. Each of these names the next one ten times. Loading them all would
// take 10^10 loads.
void TestTooManyFiles()
{
    SetFiles files;
    for (int level = 0; level < 10; level++)
    {
        Tags tags;
        for (int n = 2; n <= 11; n++)
        {
            tags["_lib" + std::to_string(n)] = "f" + std::to_string(level + 1) + ".tst";
        }

        files["f" + std::to_string(level) + ".tst"] = File<BothModes>({}, ProgramBuilder(), tags);
    }

    files["f10.tst"] = File<BothModes>({}, ProgramBuilder(), {});

    XSFLIB_CHECK(LoadError(files, "f0.tst") == "more than 64 files in the set");
}

// The calls and the outcome of a walk over files whose bytes are name=value lines, like a tag area after "[TAG]".
struct TextWalk
{
    std::vector<std::string> steps; // "parse <path> <depth>" and "add <path> <depth>", in the order of the calls
    std::optional<std::string> error;
    Tags top; // the tags of the first file
};

// Whether `tags` hold fail=`step`.
bool Fails(const Tags& tags, const std::string& step)
{
    const auto fail = tags.find("fail");
    return fail != tags.end() && fail->second == step;
}

// Walks the set at `path` among `files`, the text of each file by its path. A file with the tag fail=parse fails to
// parse. A file with fail=add fails to be added.
TextWalk WalkText(const std::map<std::string, std::string>& files, const std::string& path, const WalkRules& rules = {})
{
    SetFiles bytes;
    for (const auto& [name, text] : files)
    {
        bytes[name] = std::vector<uint8_t>(text.begin(), text.end());
    }

    TextWalk walk;
    const auto parse = [&walk](const std::string& file_path, const std::vector<uint8_t>& data, int depth,
                               PsfFile& file) -> std::optional<std::string>
    {
        walk.steps.push_back("parse " + file_path + " " + std::to_string(depth));
        file.tags = ParseTags(reinterpret_cast<const char*>(data.data()), data.size());
        if (Fails(file.tags, "parse"))
        {
            return file_path + ": can't parse";
        }

        return std::nullopt;
    };

    const auto add = [&walk](const std::string& file_path, const std::vector<uint8_t>&, int depth,
                             const PsfFile& file) -> std::optional<std::string>
    {
        walk.steps.push_back("add " + file_path + " " + std::to_string(depth));
        if (Fails(file.tags, "add"))
        {
            return file_path + ": can't add";
        }

        return std::nullopt;
    };

    PsfFile top;
    walk.error = WalkSet(path, ExactReader(bytes), "/", top, parse, add, rules);
    walk.top = top.tags;

    return walk;
}

// The number of files that `walk` parsed.
std::size_t Parsed(const TextWalk& walk)
{
    const auto parse = [](const std::string& step)
    {
        return step.starts_with("parse ");
    };

    return static_cast<std::size_t>(std::count_if(walk.steps.begin(), walk.steps.end(), parse));
}

// A walk parses each file, walks its _lib, adds the file, then walks its _lib2, _lib3 and on. An empty _libN names no
// library, and the numbers go on after it. The first file's File gives its tags.
void TestWalkOrder()
{
    const std::map<std::string, std::string> files = {
        {"dir/top", "_lib=lib\n_lib2=sub/two\n_lib3=\n_lib4=four\ntitle=Top\n"},
        {"dir/lib", "_lib=base\n"},
        {"dir/base", ""},
        {"dir/sub/two", "_lib2=three\n"},
        {"dir/sub/three", ""},
        {"dir/four", ""},
    };

    const TextWalk walk = WalkText(files, "dir/top");

    XSFLIB_CHECK(!walk.error);
    XSFLIB_CHECK(walk.steps ==
                 std::vector<std::string>({"parse dir/top 0", "parse dir/lib 1", "parse dir/base 2", "add dir/base 2",
                                           "add dir/lib 1", "add dir/top 0", "parse dir/sub/two 1", "add dir/sub/two 1",
                                           "parse dir/sub/three 2", "add dir/sub/three 2", "parse dir/four 1",
                                           "add dir/four 1"}));
    XSFLIB_CHECK(walk.top.at("title") == "Top");
}

// The first error stops a walk: a library that can't be read, a file that fails to parse, and a file that fails to be
// added.
void TestWalkErrors()
{
    const std::map<std::string, std::string> files = {
        {"orphan", "_lib=missing\n"},
        {"bad", "_lib=lib\nfail=parse\n"},
        {"refused", "_lib=lib\n_lib2=more\nfail=add\n"},
        {"lib", ""},
        {"more", ""},
    };

    const TextWalk orphan = WalkText(files, "orphan");
    const TextWalk bad = WalkText(files, "bad");
    const TextWalk refused = WalkText(files, "refused");

    XSFLIB_CHECK(orphan.error == "can't read missing" && orphan.steps == std::vector<std::string>({"parse orphan 0"}));
    XSFLIB_CHECK(bad.error == "bad: can't parse" && bad.steps == std::vector<std::string>({"parse bad 0"}));
    XSFLIB_CHECK(refused.error == "refused: can't add");
    XSFLIB_CHECK(refused.steps ==
                 std::vector<std::string>({"parse refused 0", "parse lib 1", "add lib 1", "add refused 0"}));
}

// The limits of a walk. A file that names itself nests too deep. A set of 65 files: top's _lib names a, a's _lib names
// b, and b names 62 more files as _lib2 to _lib63. By default, a file counts once it's added. Each of the 62 is added
// before a and top. With count_reached, each file counts as the walk reaches it, and the 65th file is one too many.
void TestWalkLimits()
{
    std::string many;
    for (int n = 2; n <= 63; n++)
    {
        many += "_lib" + std::to_string(n) + "=leaf\n";
    }

    const std::map<std::string, std::string> files = {
        {"loop", "_lib=loop\n"}, {"top", "_lib=a\n"}, {"a", "_lib=b\n"}, {"b", many}, {"leaf", ""}};
    WalkRules rules;
    rules.too_deep = "too deep";
    rules.too_many = "too many";
    rules.count_reached = true;

    const TextWalk loop = WalkText(files, "loop");
    const TextWalk worded_loop = WalkText(files, "loop", rules);
    const TextWalk added = WalkText(files, "top");
    const TextWalk reached = WalkText(files, "top", rules);

    XSFLIB_CHECK(loop.error == "_lib nesting too deep" && Parsed(loop) == static_cast<std::size_t>(kMaxLibDepth) + 1);
    XSFLIB_CHECK(worded_loop.error == "too deep");
    XSFLIB_CHECK(!added.error && Parsed(added) == 65 && added.steps.size() == 2 * 65);
    XSFLIB_CHECK(reached.error == "too many" && Parsed(reached) == 64);
}

// The game-mode set's driver block is checked. A mini file's memory counts too: a set plays a library's good driver
// block as a mini file patches it.
void TestDriverBlockOfSet()
{
    ProgramBuilder cut_short;
    cut_short.AddMemory(kDriverAddress, DriverBlockBytes(2, std::nullopt, false));
    ProgramBuilder good;
    good.AddMemory(kDriverAddress, DriverBlockBytes(1, 1, false));
    ProgramBuilder patch;
    patch.AddMemory(kDriverAddress + 4, Word(2, false));
    const SetFiles kFiles = {
        {"game.tst", File<BothModes>(Reserved<kProcessMagic>(), cut_short, {})},
        {"good.tst", File<BothModes>(Reserved<kProcessMagic>(), good, {})},
        {"patched.minitst", File<BothModes>({}, patch, {{"_lib", "good.tst"}})},
    };

    XSFLIB_CHECK(LoadError(kFiles, "game.tst") == "game.tst: the driver block is cut short");
    XSFLIB_CHECK(LoadError(kFiles, "good.tst").empty());
    XSFLIB_CHECK(LoadError(kFiles, "patched.minitst") ==
                 "patched.minitst: driver block version 2; this player supports version 1");
}

// A file in an archive, by foobar2000's path for it: unpack://zip|<length>|<the archive's path>|<its path in the
// archive>. With | as a separator too, the library is found in the archive. Without it, the library is looked for
// beside the archive.
void TestArchivePaths()
{
    const std::string kZip = "unpack://zip|23|file://C:\\music\\set.zip|";
    SetFiles files;
    files[kZip + "set.tst"] = Libraries()["dir/lib.tst"];
    files[kZip + "x.minitst"] = File<BothModes>({}, MiniProgram(), {{"_lib", "set.tst"}});
    LoadedSet<BothModes> set;

    XSFLIB_CHECK(!LoadSet(kZip + "x.minitst", ExactReader(files), set, "/\\|"));
    XSFLIB_CHECK(set.sources == std::vector<std::string>({kZip + "set.tst", kZip + "x.minitst"}));
    XSFLIB_CHECK(LoadError(files, kZip + "x.minitst") == "can't read unpack://zip|23|file://C:\\music\\set.tst");
}

// A format without archive mode reads no archive descriptor and skips SND chunks.
void TestGameModeOnly()
{
    ProgramBuilder with_sound;
    with_sound.AddSound(9);
    const SetFiles kFiles = {
        {"a.gmo", File<GameModeOnly>(Reserved<kArchiveMagic>(), with_sound, {})},
        {"b.gmo", File<GameModeOnly>(Reserved<kProcessMagic>(), with_sound, {})},
    };
    LoadedSet<GameModeOnly> a;
    LoadedSet<GameModeOnly> b;

    const auto a_error = LoadSet("a.gmo", ExactReader(kFiles), a);
    const auto b_error = LoadSet("b.gmo", ExactReader(kFiles), b);

    XSFLIB_CHECK(a_error == "no process descriptor in a.gmo or its libraries");
    XSFLIB_CHECK(!b_error);
    XSFLIB_CHECK(b.descriptor && !b.sound && b.sounds.empty());
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
    const auto tests = []
    {
        TestChunks();
        TestSoundLists();
        TestLoadedMemory();
        TestDriverBlock();
        TestLoadSet();
        TestSoundListsOfSets();
        TestRejectedSets();
        TestLibraryPaths();
        TestTooManyFiles();
        TestWalkOrder();
        TestWalkErrors();
        TestWalkLimits();
        TestDriverBlockOfSet();
        TestArchivePaths();
        TestGameModeOnly();
        TestTagReaders();
    };

    return xsflib::testing::RunTest("psf set", tests);
}
