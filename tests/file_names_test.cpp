// SPDX-License-Identifier: MIT

// Unit tests for the names of a rip's files (xsflib/rip/file_names.h), with an xyz format's extensions, and for the
// inputs' names, first bytes and output folders and the folders of a game's archives (xsflib/rip/tool.h).

#include "xsflib/rip/file_names.h"

#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <vector>

#include "xsflib/rip/tool.h"
#include "xsflib/testing/check.h"

using xsflib::rip::FileNames;

static void TestCharacters()
{
    FileNames names(".xyzlib", ".minixyz");

    const std::string library = names.Library("Some Game: Japan?");
    const std::string mini = names.Mini(7, "SEQ BGM/1");

    XSFLIB_CHECK(library == "Some_Game__Japan_.xyzlib");
    XSFLIB_CHECK(mini == "0007 SEQ_BGM_1.minixyz");
}

static void TestReservedNames()
{
    FileNames names(".xyzlib", ".minixyz");

    XSFLIB_CHECK(names.Library("con") == "con_.xyzlib");
    XSFLIB_CHECK(names.Library("NUL") == "NUL_.xyzlib");
    XSFLIB_CHECK(names.Library("COM0") == "COM0_.xyzlib");
    XSFLIB_CHECK(names.Library("LPT1.sound") == "LPT1_.sound.xyzlib");
    XSFLIB_CHECK(names.Library("COM10") == "COM10.xyzlib");
    XSFLIB_CHECK(names.Library("CONSOLE") == "CONSOLE.xyzlib");
    XSFLIB_CHECK(names.Library("COM\xc2\xb9") == "COM\xc2\xb9_.xyzlib");
    XSFLIB_CHECK(names.Library("lpt\xc2\xb3.sound") == "lpt\xc2\xb3_.sound.xyzlib");
    XSFLIB_CHECK(names.Library("COM\xc2\xbc") == "COM\xc2\xbc.xyzlib");
}

static void TestCharactersOutsideAscii()
{
    FileNames names(".xyzlib", ".minixyz");

    // Printable characters outside ASCII are kept, in 2, 3 or 4 bytes. Spaces and punctuation still become '_'.
    XSFLIB_CHECK(names.Library("Caf\xc3\xa9 Xyzzy abc_p") == "Caf\xc3\xa9_Xyzzy_abc_p.xyzlib");
    XSFLIB_CHECK(names.Library("\xe3\x83\x86\xe3\x82\xb9\xe3\x83\x88 (Disc 1)") ==
                 "\xe3\x83\x86\xe3\x82\xb9\xe3\x83\x88__Disc_1_.xyzlib");
    XSFLIB_CHECK(names.Library("\xf0\x9f\x8e\xb5 Music") == "\xf0\x9f\x8e\xb5_Music.xyzlib");
    XSFLIB_CHECK(names.Mini(7, "BGM_\xe3\x83\x86") == "0007 BGM_\xe3\x83\x86.minixyz");

    // A control character outside ASCII and the line and paragraph separators become one '_' each. So does each byte
    // that isn't part of a valid UTF-8 character.
    XSFLIB_CHECK(names.Library("A\xc2\x85"
                               "B\xe2\x80\xa8"
                               "C\xe2\x80\xa9"
                               "D") == "A_B_C_D.xyzlib");
    XSFLIB_CHECK(names.Library("A\x80Z\xe3\x82") == "A_Z__.xyzlib");
}

static void TestNamesDifferInMoreThanCase()
{
    FileNames names(".xyzlib", ".minixyz");

    const std::string first = names.Library("Game Sound");
    const std::string second = names.Library("game sound");
    const std::string third = names.Library("GAME_SOUND");
    const std::string first_mini = names.Mini(1, "SE");
    const std::string second_mini = names.Mini(1, "se");

    XSFLIB_CHECK(first == "Game_Sound.xyzlib");
    XSFLIB_CHECK(second == "game_sound_2.xyzlib");
    XSFLIB_CHECK(third == "GAME_SOUND_3.xyzlib");
    XSFLIB_CHECK(first_mini == "0001 SE.minixyz");
    XSFLIB_CHECK(second_mini == "0001 se_2.minixyz");
}

// The output folder that a rip tool makes next to its input when -o isn't given.
static void TestDefaultOutputDir()
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "xsflib_file_names_test";
    fs::create_directories(dir / "Game");

    const fs::path image = xsflib::rip::DefaultOutputDir((dir / "Game.iso").string(), "_xyz");
    const fs::path folder = xsflib::rip::DefaultOutputDir((dir / "Game" / "").string(), "_xyz");

    XSFLIB_CHECK(image == fs::absolute(dir / "Game_xyz").lexically_normal());
    XSFLIB_CHECK(folder == fs::absolute(dir / "Game_xyz").lexically_normal());
    XSFLIB_CHECK(xsflib::rip::NameOfInput((dir / "Game").string()) == "Game");

    fs::remove_all(dir);
}

// The first bytes of an input: as many as asked for, fewer from a short file, and none from a folder or a missing file.
static void TestReadHead()
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "xsflib_read_head_test";
    fs::create_directories(dir);
    std::ofstream(dir / "long.bin", std::ios::binary) << "YTAG\xff\xfe"
                                                         "0123456789";
    std::ofstream(dir / "short.bin", std::ios::binary) << "XTAG";

    XSFLIB_CHECK(xsflib::rip::ReadHead((dir / "long.bin").string()) ==
                 "YTAG\xff\xfe"
                 "01");
    XSFLIB_CHECK(xsflib::rip::ReadHead((dir / "long.bin").string(), 4) == "YTAG");
    XSFLIB_CHECK(xsflib::rip::ReadHead((dir / "long.bin").string(), 0x104) ==
                 "YTAG\xff\xfe"
                 "0123456789");
    XSFLIB_CHECK(xsflib::rip::ReadHead((dir / "short.bin").string()) == "XTAG");
    XSFLIB_CHECK(xsflib::rip::ReadHead(dir.string()).empty());
    XSFLIB_CHECK(xsflib::rip::ReadHead((dir / "missing.bin").string()).empty());

    fs::remove_all(dir);
}

// Archives whose names clash, ignoring case, take the folders above them until they differ.
static void TestArchiveFolders()
{
    XSFLIB_CHECK((xsflib::rip::ArchiveFolders({"content/BGM/sound.xyzar", "content/SE/sound.xyzar", "other.xyzar"}) ==
                  std::vector<std::string>{"BGM_sound", "SE_sound", "other"}));
    XSFLIB_CHECK((xsflib::rip::ArchiveFolders({"a/Sound.xyzar", "b/sound.xyzst"}) ==
                  std::vector<std::string>{"a_Sound", "b_sound"}));
}

int main()
{
    TestCharacters();
    TestCharactersOutsideAscii();
    TestDefaultOutputDir();
    TestReadHead();
    TestArchiveFolders();
    TestReservedNames();
    TestNamesDifferInMoreThanCase();

    return xsflib::testing::Summary("file name");
}
