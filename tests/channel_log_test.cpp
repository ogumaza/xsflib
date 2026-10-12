// SPDX-License-Identifier: MIT

// Tests the channel log and the WAV writer of play tests (testing/channel_log.h): the lines of a log without the sends
// and with them, the numbering of notes, and a WAV file's header and samples.

#include "xsflib/testing/channel_log.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

#include "xsflib/testing/check.h"

using namespace xsflib::testing;

namespace
{

namespace fs = std::filesystem;

std::string ReadText(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// A channel with key `key` in envelope stage `envelope`.
ChannelState Channel(uint32_t id, uint8_t key, uint32_t envelope)
{
    ChannelState c;
    c.id = id;
    c.track = 2;
    c.key = key;
    c.original_key = 60;
    c.velocity = 100.0f / 127.0f;
    c.envelope = envelope;
    c.level = -12.5f;
    c.volume = 0.5f;
    c.pitch = 1.0f;
    c.pan = -0.25f;
    c.note_pan = 0.125f;
    c.main_send = -0.5f;
    c.fx_send = {0.25f, 0.0f, 1.0f};

    return c;
}

// Writes the same frames to a log with or without the sends, and returns its text.
std::string LogText(const fs::path& dir, bool sends)
{
    const fs::path path = dir / (sends ? "sends.tsv" : "plain.tsv");
    {
        ChannelLog log(path, sends);
        log.Frame(1, 0xABCD, {});
        log.Frame(2, 0x1234, {Channel(7, 60, 0)});
        log.Frame(3, 0x1234, {Channel(7, 60, 3), Channel(9, 64, 0)}); // 7 holds its note, 9 starts one
        log.Frame(4, 0x1234, {Channel(7, 62, 0)});                    // 7 takes another key: a new note
        log.Frame(5, 0x1234, {Channel(7, 62, 4)});
        log.Frame(6, 0x1234, {Channel(7, 62, 0)}); // out of its release: a new note
    }

    return ReadText(path);
}

void TestLogs(const fs::path& dir)
{
    // A line of frames 2 to 6, with the random numbers' state 0x1234.
    const auto line = [](int frame, int note, uint32_t id, int key, uint32_t envelope, bool sends)
    {
        char text[256];
        std::snprintf(text, sizeof(text),
                      "%d\t00001234\t%d\t%08x\t2\t%d\t60\t0.787401557\t%u\t-12.5\t0.5\t1\t-0.25\t0\t0.125%s\n", frame,
                      note, id, key, envelope, sends ? "\t-0.5\t0.25\t0\t1" : "");

        return std::string(text);
    };

    for (const bool sends : {false, true})
    {
        std::string expected =
            "frame\trandom\tnote\tchannel\ttrack\tkey\toriginal_key\tvelocity\tenvelope\tlevel\t"
            "volume\tpitch\tpan\tspan\tnote_pan";
        expected += sends ? "\tmain_send\tfx_send_a\tfx_send_b\tfx_send_c\n" : "\n";
        expected += std::string("1\t0000abcd\t0\t00000000\t-1\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0") +
                    (sends ? "\t0\t0\t0\t0\n" : "\n");
        expected += line(2, 1, 7, 60, 0, sends);
        expected += line(3, 1, 7, 60, 3, sends);
        expected += line(3, 2, 9, 64, 0, sends);
        expected += line(4, 3, 7, 62, 0, sends);
        expected += line(5, 3, 7, 62, 4, sends);
        expected += line(6, 4, 7, 62, 0, sends);

        Check(LogText(dir, sends) == expected, sends ? "the log with the sends" : "the log without the sends");
    }
}

void TestWav(const fs::path& dir)
{
    const fs::path path = dir / "out.wav";
    const std::string kExpected = std::string(
        "RIFF\x2c\0\0\0WAVEfmt \x10\0\0\0\x01\0\x02\0\x80\xbb\0\0\0\xee\x02\0"
        "\x04\0\x10\0data\x08\0\0\0\x01\0\xfe\xff\xff\x7f\0\x80",
        52);

    WriteWav(path, {1, -2, 0x7FFF, -0x8000});
    const std::string wav = ReadText(path);

    Check(wav == kExpected, "a WAV file of two stereo frames");
}

} // namespace

int main()
{
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path() / "xsflib_channel_log_test";
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    TestLogs(dir);
    TestWav(dir);

    fs::remove_all(dir, ec);

    if (failures)
    {
        std::fprintf(stderr, "%d check(s) failed\n", failures);

        return 1;
    }

    return 0;
}
