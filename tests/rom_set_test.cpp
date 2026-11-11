// SPDX-License-Identifier: MIT

// The reader of ZIP archives and the loader of ROM sets. The archives come from xsflib/testing/zip_archive.h. The
// deflated entry is a raw deflate stream that zlib wrote.

#include "xsflib/rip/rom_set.h"

#include <cstdint>
#include <filesystem>
#include <iterator>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "xsflib/files.h"
#include "xsflib/rip/zip.h"
#include "xsflib/sha1.h"
#include "xsflib/testing/check.h"
#include "xsflib/testing/temp_folder.h"
#include "xsflib/testing/zip_archive.h"

namespace
{

using xsflib::rip::RomChip;
using xsflib::rip::RomSetGame;
using xsflib::rip::SetFile;
using xsflib::rip::ZipEntry;
using xsflib::testing::DeflatedEntry;
using xsflib::testing::StoredEntry;
using xsflib::testing::ZipArchive;
using xsflib::testing::ZipArchiveEntry;

// "xyz xyz xyz xyz xyz xyz\n", deflated.
constexpr uint8_t kDeflated[] = {0xAB, 0xA8, 0xAC, 0x52, 0xA8, 0xC0, 0xC4, 0x5C, 0x00};
constexpr char kInflated[] = "xyz xyz xyz xyz xyz xyz\n";

// An empty file, deflated: one fixed Huffman block that holds only its end code.
constexpr uint8_t kDeflatedEmpty[] = {0x03, 0x00};

// The chips of a game of two chips.
const std::vector<uint8_t> kLow = {0x37, 0x80, 0x40, 0x12};
const std::vector<uint8_t> kHigh = {5, 6, 7, 8};

void TestReadEntries()
{
    const std::vector<uint8_t> text(kInflated, kInflated + sizeof(kInflated) - 1);
    const std::vector<uint8_t> zip = ZipArchive({
        StoredEntry("a.bin", {1, 2, 3, 4}),
        StoredEntry("folder/", {}),
        DeflatedEntry("b.txt", {std::begin(kDeflated), std::end(kDeflated)}, text),
        DeflatedEntry("empty", {std::begin(kDeflatedEmpty), std::end(kDeflatedEmpty)}, {}),
    });

    std::vector<ZipEntry> entries;
    const auto problem = xsflib::rip::ReadZip(zip, entries, "the test");

    XSFLIB_CHECK(xsflib::rip::LooksLikeZip(zip));
    XSFLIB_CHECK(!problem);
    XSFLIB_REQUIRE(entries.size() == 3);
    XSFLIB_CHECK(entries[0].name == "a.bin" && entries[0].data == std::vector<uint8_t>({1, 2, 3, 4}));
    XSFLIB_CHECK(entries[1].name == "b.txt" && entries[1].data == text);
    XSFLIB_CHECK(entries[2].name == "empty" && entries[2].data.empty());
}

void TestRejectDamage()
{
    std::vector<uint8_t> zip = ZipArchive({StoredEntry("a.bin", {1, 2, 3, 4})});
    zip[30 + 5] ^= 0xFF; // A byte of the entry's data, past the local header and the name.
    const std::vector<uint8_t> cut(zip.begin(), zip.begin() + 20);

    std::vector<ZipEntry> entries;
    const auto crc = xsflib::rip::ReadZip(zip, entries, "the test");
    const auto short_archive = xsflib::rip::ReadZip(cut, entries, "the test");

    XSFLIB_CHECK(crc && crc->find("CRC-32") != std::string::npos);
    XSFLIB_CHECK(short_archive.has_value());
}

std::string Sha1(const std::vector<uint8_t>& data)
{
    return xsflib::Sha1::Hex(xsflib::Sha1::Of(data));
}

// The chips of a game: a byte-swapped one at offset 0 and a plain one at offset 4.
std::vector<RomChip> TwoChips(const std::vector<uint8_t>& low, const std::vector<uint8_t>& high)
{
    return {
        RomChip{Sha1(low), static_cast<uint32_t>(low.size()), 0, true},
        RomChip{Sha1(high), static_cast<uint32_t>(high.size()), 4, false},
    };
}

void TestBuildImage()
{
    const std::vector<RomChip> chips = TwoChips(kLow, kHigh);
    // A game with no chips comes first. It never matches, and the game that does keeps its index.
    const std::vector<RomSetGame> games = {{"No Chips", {}}, {"Two Chips", chips}};
    const std::vector<SetFile> files = {{"other", {9}}, {"high", kHigh}, {"low", kLow}};

    const xsflib::rip::RomSetImage image = xsflib::rip::BuildRomSetImage(games, files);
    const xsflib::rip::RomSetImage missing = xsflib::rip::BuildRomSetImage(games, {{"low", kLow}});
    const xsflib::rip::RomSetImage none = xsflib::rip::BuildRomSetImage(games, {{"other", {9}}});

    XSFLIB_CHECK(image.game == 1u);
    XSFLIB_CHECK(image.rom == std::vector<uint8_t>({0x80, 0x37, 0x12, 0x40, 5, 6, 7, 8}));
    XSFLIB_CHECK(!missing.game.has_value());
    XSFLIB_CHECK(missing.problem.find("the set of Two Chips is missing 1 of its 2 chips") != std::string::npos);
    XSFLIB_CHECK(missing.problem.find(Sha1(kHigh)) != std::string::npos);
    XSFLIB_CHECK(!none.game.has_value() && none.problem == "no profile's chips are in the set");
}

void TestNameTheProgram()
{
    ZipArchiveEntry entry = StoredEntry("a.bin", {1, 2, 3, 4});
    entry.method = 12; // bzip2, which the reader doesn't read.
    const std::vector<uint8_t> zip = ZipArchive({entry});

    std::vector<ZipEntry> entries;
    const auto problem = xsflib::rip::ReadZip(zip, entries, "xyzrip");

    XSFLIB_CHECK(problem == "a.bin uses a compression method that xyzrip doesn't read");
}

void TestReadSets()
{
    const std::vector<RomChip> chips = TwoChips(kLow, kHigh);
    const std::vector<RomSetGame> games = {{"Two Chips", chips}};
    const std::set<uint64_t> sizes = {kLow.size(), kHigh.size()};
    const xsflib::testing::TempFolder folder("xsflib_rom_set_test");
    const std::vector<uint8_t> zip = ZipArchive({StoredEntry("x", kLow), StoredEntry("y", kHigh)});
    const std::vector<uint8_t> other = {'1', '2', '3', '4', '5'};
    std::filesystem::create_directory(folder.Path() / "chips");
    XSFLIB_REQUIRE(!xsflib::WriteFile(folder.Path() / "set.zip", zip));
    XSFLIB_REQUIRE(!xsflib::WriteFile(folder.Path() / "chips" / "x", kLow));
    XSFLIB_REQUIRE(!xsflib::WriteFile(folder.Path() / "chips" / "y", kHigh));
    XSFLIB_REQUIRE(!xsflib::WriteFile(folder.Path() / "chips" / "other", other));

    // The sizes of the chips come from `sizes`, or from the games.
    std::vector<SetFile> from_zip;
    std::vector<SetFile> from_folder;
    std::vector<SetFile> from_games;
    const auto zip_problem = xsflib::rip::ReadRomSet(folder.Path() / "set.zip", sizes, "the test", from_zip);
    const auto folder_problem = xsflib::rip::ReadRomSet(folder.Path() / "chips", sizes, "the test", from_folder);
    const auto games_problem = xsflib::rip::ReadRomSet(folder.Path() / "chips", games, "the test", from_games);

    XSFLIB_CHECK(xsflib::rip::IsRomSet(folder.Path() / "set.zip"));
    XSFLIB_CHECK(xsflib::rip::IsRomSet(folder.Path() / "chips"));
    XSFLIB_CHECK(!xsflib::rip::IsRomSet(folder.Path() / "chips" / "x"));
    XSFLIB_CHECK(!zip_problem && from_zip.size() == 2);
    XSFLIB_CHECK(!folder_problem && from_folder.size() == 2);
    XSFLIB_CHECK(!games_problem && from_games.size() == 2);
    XSFLIB_CHECK(xsflib::rip::BuildRomSetImage(games, from_zip).game == 0u);
    XSFLIB_CHECK(xsflib::rip::BuildRomSetImage(games, from_folder).game == 0u);
    XSFLIB_CHECK(xsflib::rip::BuildRomSetImage(games, from_games).game == 0u);
}

void TestOnlyFileWithAnExtension()
{
    constexpr std::string_view kExtensions[] = {".xyz", ".zyx"};
    std::vector<SetFile> one = {{"readme.txt", {}}, {"folder/Game.XYZ", {1}}, {"xyz", {2}}};
    std::vector<SetFile> two = {{"a.xyz", {1}}, {"b.ZyX", {2}}};
    std::vector<SetFile> none = {{"a.xyz.txt", {1}}, {"xyz", {2}}};

    const SetFile* found = xsflib::rip::OnlyFileWithExtension(one, kExtensions);
    const SetFile* of_two = xsflib::rip::OnlyFileWithExtension(two, kExtensions);
    const SetFile* of_none = xsflib::rip::OnlyFileWithExtension(none, kExtensions);

    XSFLIB_CHECK(found == &one[1]);
    XSFLIB_CHECK(of_two == nullptr);
    XSFLIB_CHECK(of_none == nullptr);
}

} // namespace

int main()
{
    const auto tests = []
    {
        TestReadEntries();
        TestRejectDamage();
        TestBuildImage();
        TestNameTheProgram();
        TestReadSets();
        TestOnlyFileWithAnExtension();
    };

    return xsflib::testing::RunTest("rom set", tests);
}
