// SPDX-License-Identifier: MIT

// The reader of ZIP archives and the loader of ROM sets. The archives are built here in the layout of PKWARE's
// APPNOTE.TXT. The deflated entry is a raw deflate stream that zlib wrote.

#include "xsflib/rip/rom_set.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "xsflib/rip/zip.h"
#include "xsflib/sha1.h"
#include "xsflib/testing/check.h"
#include "xsflib/testing/temp_folder.h"

namespace
{

using xsflib::rip::RomChip;
using xsflib::rip::RomSetGame;
using xsflib::rip::SetFile;
using xsflib::rip::ZipEntry;

// "xyz xyz xyz xyz xyz xyz\n", deflated.
constexpr uint8_t kDeflated[] = {0xAB, 0xA8, 0xAC, 0x52, 0xA8, 0xC0, 0xC4, 0x5C, 0x00};
constexpr char kInflated[] = "xyz xyz xyz xyz xyz xyz\n";

// An empty file, deflated: one fixed Huffman block that holds only its end code.
constexpr uint8_t kDeflatedEmpty[] = {0x03, 0x00};

uint32_t Crc32(const std::vector<uint8_t>& data)
{
    uint32_t crc = 0xFFFFFFFF;
    for (uint8_t byte : data)
    {
        crc ^= byte;
        for (int bit = 0; bit < 8; bit++)
        {
            crc = (crc >> 1) ^ ((crc & 1) != 0 ? 0xEDB88320 : 0);
        }
    }

    return ~crc;
}

void Put16(std::vector<uint8_t>& out, uint32_t value)
{
    out.push_back(static_cast<uint8_t>(value));
    out.push_back(static_cast<uint8_t>(value >> 8));
}

void Put32(std::vector<uint8_t>& out, uint32_t value)
{
    Put16(out, value & 0xFFFF);
    Put16(out, value >> 16);
}

struct Entry
{
    std::string name;
    uint16_t method = 0;
    std::vector<uint8_t> stored; // The bytes in the archive.
    std::vector<uint8_t> data;   // The bytes once read.
};

// An archive of `entries`, with each entry's CRC-32 of its data.
std::vector<uint8_t> MakeZip(const std::vector<Entry>& entries)
{
    std::vector<uint8_t> zip;
    std::vector<uint32_t> offsets;
    for (const Entry& entry : entries)
    {
        offsets.push_back(static_cast<uint32_t>(zip.size()));
        Put32(zip, 0x04034B50);
        Put16(zip, 20);
        Put16(zip, 0);
        Put16(zip, entry.method);
        Put32(zip, 0);
        Put32(zip, Crc32(entry.data));
        Put32(zip, static_cast<uint32_t>(entry.stored.size()));
        Put32(zip, static_cast<uint32_t>(entry.data.size()));
        Put16(zip, static_cast<uint32_t>(entry.name.size()));
        Put16(zip, 0);
        zip.insert(zip.end(), entry.name.begin(), entry.name.end());
        zip.insert(zip.end(), entry.stored.begin(), entry.stored.end());
    }

    const uint32_t directory = static_cast<uint32_t>(zip.size());
    for (size_t i = 0; i < entries.size(); i++)
    {
        const Entry& entry = entries[i];
        Put32(zip, 0x02014B50);
        Put16(zip, 20);
        Put16(zip, 20);
        Put16(zip, 0);
        Put16(zip, entry.method);
        Put32(zip, 0);
        Put32(zip, Crc32(entry.data));
        Put32(zip, static_cast<uint32_t>(entry.stored.size()));
        Put32(zip, static_cast<uint32_t>(entry.data.size()));
        Put16(zip, static_cast<uint32_t>(entry.name.size()));
        Put32(zip, 0);
        Put32(zip, 0);
        Put32(zip, 0);
        Put32(zip, offsets[i]);
        zip.insert(zip.end(), entry.name.begin(), entry.name.end());
    }

    const uint32_t directory_size = static_cast<uint32_t>(zip.size()) - directory;
    Put32(zip, 0x06054B50);
    Put32(zip, 0);
    Put16(zip, static_cast<uint32_t>(entries.size()));
    Put16(zip, static_cast<uint32_t>(entries.size()));
    Put32(zip, directory_size);
    Put32(zip, directory);
    Put16(zip, 0);

    return zip;
}

Entry Stored(const std::string& name, const std::vector<uint8_t>& data)
{
    return {name, 0, data, data};
}

void ReadsEntries()
{
    const std::vector<uint8_t> text(kInflated, kInflated + sizeof(kInflated) - 1);
    const std::vector<uint8_t> zip = MakeZip({
        Stored("a.bin", {1, 2, 3, 4}),
        Stored("folder/", {}),
        {"b.txt", 8, std::vector<uint8_t>(std::begin(kDeflated), std::end(kDeflated)), text},
        {"empty", 8, std::vector<uint8_t>(std::begin(kDeflatedEmpty), std::end(kDeflatedEmpty)), {}},
    });

    std::vector<ZipEntry> entries;
    const auto problem = xsflib::rip::ReadZip(zip, entries, "the test");

    XSFLIB_CHECK(xsflib::rip::LooksLikeZip(zip));
    XSFLIB_CHECK(!problem);
    XSFLIB_CHECK(entries.size() == 3);
    XSFLIB_CHECK(entries[0].name == "a.bin" && entries[0].data == std::vector<uint8_t>({1, 2, 3, 4}));
    XSFLIB_CHECK(entries[1].name == "b.txt" && entries[1].data == text);
    XSFLIB_CHECK(entries[2].name == "empty" && entries[2].data.empty());
}

void RejectsDamage()
{
    Entry entry = Stored("a.bin", {1, 2, 3, 4});
    std::vector<uint8_t> zip = MakeZip({entry});
    zip[30 + 5] ^= 0xFF; // A byte of the entry's data, past the local header and the name.
    std::vector<uint8_t> cut(zip.begin(), zip.begin() + 20);

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

void BuildsImage()
{
    const std::vector<uint8_t> low = {0x37, 0x80, 0x40, 0x12};
    const std::vector<uint8_t> high = {5, 6, 7, 8};
    const std::vector<RomChip> chips = TwoChips(low, high);
    // A game with no chips comes first. It never matches, and the game that does keeps its index.
    const std::vector<RomSetGame> games = {{"No Chips", {}}, {"Two Chips", chips}};
    const std::vector<SetFile> files = {{"other", {9}}, {"high", high}, {"low", low}};

    const xsflib::rip::RomSetImage image = xsflib::rip::BuildRomSetImage(games, files);
    const xsflib::rip::RomSetImage missing = xsflib::rip::BuildRomSetImage(games, {{"low", low}});
    const xsflib::rip::RomSetImage none = xsflib::rip::BuildRomSetImage(games, {{"other", {9}}});

    XSFLIB_CHECK(image.game == 1u);
    XSFLIB_CHECK(image.rom == std::vector<uint8_t>({0x80, 0x37, 0x12, 0x40, 5, 6, 7, 8}));
    XSFLIB_CHECK(!missing.game.has_value());
    XSFLIB_CHECK(missing.problem.find("the set of Two Chips is missing 1 of its 2 chips") != std::string::npos);
    XSFLIB_CHECK(missing.problem.find(Sha1(high)) != std::string::npos);
    XSFLIB_CHECK(!none.game.has_value() && none.problem == "no profile's chips are in the set");
}

void NamesTheProgram()
{
    Entry entry = Stored("a.bin", {1, 2, 3, 4});
    entry.method = 12; // bzip2, which the reader doesn't read.
    const std::vector<uint8_t> zip = MakeZip({entry});

    std::vector<ZipEntry> entries;
    const auto problem = xsflib::rip::ReadZip(zip, entries, "xyzrip");

    XSFLIB_CHECK(problem == "a.bin uses a compression method that xyzrip doesn't read");
}

void ReadsSets()
{
    const std::vector<uint8_t> low = {0x37, 0x80, 0x40, 0x12};
    const std::vector<uint8_t> high = {5, 6, 7, 8};
    const std::vector<RomChip> chips = TwoChips(low, high);
    const std::vector<RomSetGame> games = {{"Two Chips", chips}};
    const std::set<uint64_t> sizes = {low.size(), high.size()};
    const xsflib::testing::TempFolder folder("xsflib_rom_set_test");
    const std::vector<uint8_t> zip = MakeZip({Stored("x", low), Stored("y", high)});
    {
        std::ofstream(folder.Path() / "set.zip", std::ios::binary)
            .write(reinterpret_cast<const char*>(zip.data()), static_cast<std::streamsize>(zip.size()));
        std::filesystem::create_directory(folder.Path() / "chips");
        std::ofstream(folder.Path() / "chips" / "x", std::ios::binary)
            .write(reinterpret_cast<const char*>(low.data()), static_cast<std::streamsize>(low.size()));
        std::ofstream(folder.Path() / "chips" / "y", std::ios::binary)
            .write(reinterpret_cast<const char*>(high.data()), static_cast<std::streamsize>(high.size()));
        std::ofstream(folder.Path() / "chips" / "other", std::ios::binary).write("12345", 5);
    }

    std::vector<SetFile> from_zip;
    std::vector<SetFile> from_folder;
    const auto zip_problem = xsflib::rip::ReadRomSet(folder.Path() / "set.zip", sizes, "the test", from_zip);
    const auto folder_problem = xsflib::rip::ReadRomSet(folder.Path() / "chips", sizes, "the test", from_folder);

    XSFLIB_CHECK(xsflib::rip::IsRomSet(folder.Path() / "set.zip"));
    XSFLIB_CHECK(xsflib::rip::IsRomSet(folder.Path() / "chips"));
    XSFLIB_CHECK(!xsflib::rip::IsRomSet(folder.Path() / "chips" / "x"));
    XSFLIB_CHECK(!zip_problem && from_zip.size() == 2);
    XSFLIB_CHECK(!folder_problem && from_folder.size() == 2);
    XSFLIB_CHECK(xsflib::rip::BuildRomSetImage(games, from_zip).game == 0u);
    XSFLIB_CHECK(xsflib::rip::BuildRomSetImage(games, from_folder).game == 0u);
}

void FindsTheOnlyFileWithAnExtension()
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
    return xsflib::testing::RunTest("rom_set",
                                    []
                                    {
                                        ReadsEntries();
                                        RejectsDamage();
                                        BuildsImage();
                                        NamesTheProgram();
                                        ReadsSets();
                                        FindsTheOnlyFileWithAnExtension();
                                    });
}
