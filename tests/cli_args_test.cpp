// SPDX-License-Identifier: MIT

// Unit tests for the messages of the play and rip tools' command lines (xsflib/cli/play.h, xsflib/cli/rip.h and
// xsflib/cli/tool.h): the help, the version, a missing value, a bad value, an unknown option, a missing input, and
// the options that reach the tool. play_test and rip_frame_test check the exit codes. These checks read what the tools
// print to stdout and stderr, word for word.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

#include "xsflib/cli/play.h"
#include "xsflib/cli/rip.h"
#include "xsflib/cli/tool.h"
#include "xsflib/testing/check.h"
#include "xsflib/testing/command_line.h"
#include "xsflib/testing/temp_folder.h"

namespace
{

namespace fs = std::filesystem;

// The descriptors of the C streams, for Redirect.
int Descriptor(std::FILE* stream)
{
#if defined(_WIN32)
    return _fileno(stream);
#else
    return fileno(stream);
#endif
}

int Duplicate(int descriptor)
{
#if defined(_WIN32)
    return _dup(descriptor);
#else
    return dup(descriptor);
#endif
}

void Replace(int from, int to)
{
#if defined(_WIN32)
    _dup2(from, to);
#else
    dup2(from, to);
#endif
}

void CloseDescriptor(int descriptor)
{
#if defined(_WIN32)
    _close(descriptor);
#else
    close(descriptor);
#endif
}

// Sends what `stream` prints to the file at `path` while the object exists.
class Redirect
{
public:
    Redirect(std::FILE* stream, const fs::path& path) : stream_(stream)
    {
        std::fflush(stream_);
        saved_ = Duplicate(Descriptor(stream_));
        file_ = std::fopen(path.string().c_str(), "wb");
        Replace(Descriptor(file_), Descriptor(stream_));
    }

    Redirect(const Redirect&) = delete;
    Redirect& operator=(const Redirect&) = delete;

    ~Redirect()
    {
        std::fflush(stream_);
        Replace(saved_, Descriptor(stream_));
        CloseDescriptor(saved_);
        std::fclose(file_);
    }

private:
    std::FILE* stream_;
    std::FILE* file_ = nullptr;
    int saved_ = -1;
};

std::string ReadText(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// What a call returned and printed.
struct Printed
{
    int result = 0;
    std::string out;
    std::string err;
};

// Runs `call` with stdout and stderr sent to files in `folder`.
Printed Capture(const fs::path& folder, const std::function<int()>& call)
{
    Printed printed;
    {
        const Redirect out(stdout, folder / "stdout.txt");
        const Redirect err(stderr, folder / "stderr.txt");
        printed.result = call();
    }

    printed.out = ReadText(folder / "stdout.txt");
    printed.err = ReadText(folder / "stderr.txt");

    return printed;
}

// The least that the play tool's options require of a player.
struct TestPlayer
{
    static constexpr int kSampleRate = 1000;
};

xsflib::cli::PlayTool<TestPlayer> MakePlayTool()
{
    xsflib::cli::PlayTool<TestPlayer> tool;
    tool.name = "testplay";
    tool.version = "1.0.0";
    tool.description = "Renders test files.\n";

    return tool;
}

// Reads a play tool's command line.
Printed ParsePlay(const fs::path& folder, std::initializer_list<std::string> args,
                  xsflib::cli::detail::PlayOptions<TestPlayer>& o)
{
    const xsflib::cli::PlayTool<TestPlayer> tool = MakePlayTool();
    xsflib::testing::CommandLine line("testplay", args);
    std::vector<std::string> inputs;

    return Capture(folder, [&] { return xsflib::cli::detail::ParseArgs(tool, line.Argc(), line.Argv(), o, inputs); });
}

Printed ParsePlay(const fs::path& folder, std::initializer_list<std::string> args)
{
    xsflib::cli::detail::PlayOptions<TestPlayer> o;
    return ParsePlay(folder, args, o);
}

void TestPlay(const fs::path& folder)
{
    // The help goes to stdout after -h, and to stderr when there are no inputs.
    const Printed help = ParsePlay(folder, {"-h"});

    XSFLIB_CHECK(help.result == -1 && help.err.empty());
    XSFLIB_CHECK(help.out.starts_with("usage: testplay [options] <file> [<file> ...]\n\nRenders test files.\n\n"));
    XSFLIB_CHECK(help.out.ends_with("  -h, --help         show this help\n"));
    XSFLIB_CHECK(ParsePlay(folder, {"--help"}).out == help.out);

    const Printed usage = ParsePlay(folder, {});

    XSFLIB_CHECK(usage.result == 2 && usage.out.empty() && usage.err == help.out);

    const Printed version = ParsePlay(folder, {"--version"});

    XSFLIB_CHECK(version.result == -1 && version.out == "testplay 1.0.0\n" && version.err.empty());

    // The errors.
    const auto error = [&folder](std::initializer_list<std::string> args)
    {
        const Printed printed = ParsePlay(folder, args);
        return printed.result == 2 && printed.out.empty() ? printed.err : "exit " + std::to_string(printed.result);
    };

    XSFLIB_CHECK(error({"-o"}) == "error: -o requires a value\n");
    XSFLIB_CHECK(error({"a", "--length"}) == "error: --length requires a value\n");
    XSFLIB_CHECK(error({"--fade", "x", "a"}) == "error: bad time for --fade: 'x'\n");
    XSFLIB_CHECK(error({"--length", "", "a"}) == "error: bad time for --length: ''\n");
    XSFLIB_CHECK(error({"--bogus", "a"}) == "error: unknown option --bogus (see --help)\n");
    XSFLIB_CHECK(error({"a", "b", "-o", "out.wav"}) == "error: -o requires a single input file\n");

    // The options reach the playback. A lone "-" is an input.
    xsflib::cli::detail::PlayOptions<TestPlayer> o;
    const Printed parsed = ParsePlay(
        folder, {"--default", "1:00", "--fade", "2", "--length", "0:01.5", "-o", "out.wav", "--info", "-"}, o);

    XSFLIB_CHECK(parsed.result == 0 && parsed.out.empty() && parsed.err.empty());
    XSFLIB_CHECK(o.playback.default_length_ms == 60000 && o.playback.fade_override_ms == 2000);
    XSFLIB_CHECK(o.playback.length_override_ms == 1500 && o.out == "out.wav" && o.info);
}

struct TestOptions : xsflib::cli::RipOptions
{
    std::string firmware;
};

// A rip tool with two options of its own: --firmware, and --level, which takes only 1.
xsflib::cli::RipTool<TestOptions> MakeRipTool()
{
    xsflib::cli::RipTool<TestOptions> tool;
    tool.name = "tstrip";
    tool.version = "1.2.3";
    tool.usage = "usage: tstrip [options] <input>...\n";
    tool.requires_a_value = "needs a value";
    tool.parse_option = [](const std::string& option, const std::function<std::string()>& next, TestOptions& o)
    {
        if (option == "--firmware")
        {
            o.firmware = next();
            return 0;
        }

        if (option != "--level")
        {
            return -1;
        }

        const std::string level = next();
        if (level != "1")
        {
            std::fprintf(stderr, "error: bad level '%s'\n", level.c_str());
            return 2;
        }

        return 0;
    };

    return tool;
}

// Reads a rip tool's command line.
Printed ParseRip(const fs::path& folder, std::initializer_list<std::string> args, TestOptions& o)
{
    const xsflib::cli::RipTool<TestOptions> tool = MakeRipTool();
    xsflib::testing::CommandLine line("tstrip", args);
    std::vector<std::string> inputs;

    return Capture(folder,
                   [&] { return xsflib::cli::detail::ParseRipArgs(tool, line.Argc(), line.Argv(), o, inputs); });
}

Printed ParseRip(const fs::path& folder, std::initializer_list<std::string> args)
{
    TestOptions o;
    return ParseRip(folder, args, o);
}

void TestRip(const fs::path& folder)
{
    // The help, as the format gives it.
    const Printed help = ParseRip(folder, {"--help"});

    XSFLIB_CHECK(help.result == -1 && help.out == "usage: tstrip [options] <input>...\n" && help.err.empty());

    const Printed usage = ParseRip(folder, {});

    XSFLIB_CHECK(usage.result == 2 && usage.out.empty() && usage.err == help.out);

    const Printed version = ParseRip(folder, {"--version"});

    XSFLIB_CHECK(version.result == -1 && version.out == "tstrip 1.2.3\n" && version.err.empty());

    // The errors. --jobs without a value is a bad number of threads. A format's option prints the error itself.
    const auto error = [&folder](std::initializer_list<std::string> args)
    {
        const Printed printed = ParseRip(folder, args);
        return printed.result == 2 && printed.out.empty() ? printed.err : "exit " + std::to_string(printed.result);
    };

    XSFLIB_CHECK(error({"a", "--game"}) == "error: --game needs a value\n");
    XSFLIB_CHECK(error({"--firmware"}) == "error: --firmware needs a value\n");
    XSFLIB_CHECK(error({"--level", "x", "a"}) == "error: bad level 'x'\n");
    XSFLIB_CHECK(error({"--jobs"}) == "error: --jobs takes a number of threads from 1 to 1024, not ''\n");
    XSFLIB_CHECK(error({"--jobs", "0", "a"}) == "error: --jobs takes a number of threads from 1 to 1024, not '0'\n");
    XSFLIB_CHECK(error({"--mode", "nope", "a"}) == "error: --mode must be game or archive\n");
    XSFLIB_CHECK(error({"--only", "[", "a"}).starts_with("error: --only: "));
    XSFLIB_CHECK(error({"--bogus", "a"}) == "error: unknown option --bogus (see --help)\n");

    // The options reach the rip.
    TestOptions o;
    const Printed parsed =
        ParseRip(folder, {"--output",  "out",    "--list",     "--no-length", "--bgm",   "--mode", "game",
                          "--archive", "x",      "--only",     "^SEQ_",       "--game",  "G",      "--artist",
                          "A",         "--year", "2001",       "--copyright", "C",       "--by",   "B",
                          "--jobs",    "3",      "--firmware", "f.bin",       "--level", "1",      "a"},
                 o);

    XSFLIB_CHECK(parsed.result == 0 && parsed.out.empty() && parsed.err.empty());
    XSFLIB_CHECK(o.out_dir == "out" && o.list && !o.lengths && o.bgm && o.mode == "game" && o.only_archive == "x");
    XSFLIB_CHECK(o.only_text == "^SEQ_" && o.game_name == "G" && o.artist == "A" && o.year == "2001");
    XSFLIB_CHECK(o.copyright == "C" && o.by == "B" && o.jobs == 3 && o.firmware == "f.bin");
}

void TestInputs(const fs::path& folder)
{
    // An input that doesn't exist stops the run. After the first input, the error says what -o names.
    const fs::path a = folder / "a";
    const fs::path missing = folder / "missing";
    std::ofstream(a).put('a');
    int runs = 0;
    const auto run_inputs = [&](std::initializer_list<std::string> args)
    {
        const auto parse = [&args](std::vector<std::string>& inputs)
        {
            inputs = args;
            return 0;
        };

        const auto before = [](std::vector<std::string>&)
        {
            return 0;
        };

        const auto run = [&runs](const std::string&)
        {
            runs++;
            return 0;
        };

        return Capture(folder, [&] { return xsflib::cli::detail::RunInputs(parse, "folder", before, run); });
    };

    const Printed first = run_inputs({missing.string()});

    XSFLIB_CHECK(first.result == 1 && first.err == "error: " + missing.string() + ": no such file or folder\n");

    const Printed second = run_inputs({a.string(), missing.string()});

    XSFLIB_CHECK(second.result == 1 && runs == 0);
    XSFLIB_CHECK(second.err ==
                 "error: " + missing.string() + ": no such file or folder (-o names the output folder)\n");
}

} // namespace

int main()
{
    const auto tests = []
    {
        const xsflib::testing::TempFolder temp("xsflib_cli_args_test");

        TestPlay(temp.Path());
        TestRip(temp.Path());
        TestInputs(temp.Path());
    };

    return xsflib::testing::RunTest("cli args", tests);
}
