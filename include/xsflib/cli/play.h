// SPDX-License-Identifier: MIT

// The frame of a format's play tool: it renders each file to WAV or FLAC at the player's rate.
//
// usage: <tool> [options] <file> [<file> ...]
//
// PrintUsage gives the options: the output file of a single input (-o), the times in place of the tags (--length,
// --fade and --default), --info, --version and --help. Without -o, the tool renders each file to a WAV file beside it,
// with .wav in place of the file's extension.
//
// The files are rendered one after another. A file that fails doesn't stop the rest. A library among the inputs is
// skipped. The other files load it. Files dropped on the program become inputs.
//
// Each format gives its names, its description and the wording of some messages in a PlayTool. The format's main calls
// RunPlayTool.

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <string>
#include <system_error>
#include <vector>

#include "xsflib/ascii.h"
#include "xsflib/cli/audio_out.h"
#include "xsflib/cli/tool.h"
#include "xsflib/player/playback.h"
#include "xsflib/psf/psf.h"

namespace xsflib::cli
{

// A number of frames at `sample_rate` as a PSF time, to the nearest millisecond.
inline std::string FramesToTime(uint64_t frames, int sample_rate)
{
    const auto rate = static_cast<uint64_t>(sample_rate);
    return psf::FormatTime(static_cast<long long>((frames * 1000 + rate / 2) / rate));
}

// What differs between the formats' play tools: the names, the help, the extensions, some words of the messages and two
// hooks.
template <typename Player>
struct PlayTool
{
    // The tool's name ("xyzplay") and version.
    std::string name;
    std::string version;

    // The help. It gives the usage line, `description`, and the options. `description` says what the tool does, in
    // lines that end with a newline. The -o and --info lines take the tool's wording.
    std::string description;
    std::string output_option = "write FILE instead (one input only). The extension selects WAV or FLAC.";
    std::string info_option = "print the tags and the files each input loads, and don't render";

    // The extensions of the format's libraries and minis (".xyzlib" and ".minixyz").
    std::string library_extension;
    std::string mini_extension;

    // The format's file with its article ("a zsf file", "an xyz file").
    std::string a_file;

    // The words in "error: --length requires a value".
    std::string requires_a_value = "requires a value";

    // What the time before rendering is called in the report ("after 0.12 s of boot").
    std::string set_up = "boot";

    // What the set holds, such as "2 files", at the start of the summary line of --info. The line then gives the length
    // and fade.
    std::function<std::string(player::Playback<Player>&)> describe_set;

    // Runs once before the first input, after the arguments are checked.
    std::function<void()> before_inputs;
};

// What a set that loads memory chunks and files holds (its `memory` and `files`, like psf/program.h's ProgramChunks):
// "3 memory chunks, 2 files".
template <typename Set>
std::string DescribeChunks(const Set& set)
{
    return std::to_string(set.memory.size()) + " memory chunks, " + std::to_string(set.files.size()) + " files";
}

// A describe_set for a player whose loaded set holds memory chunks and files (see DescribeChunks).
template <typename Player>
std::string DescribeLoadedChunks(player::Playback<Player>& playback)
{
    return DescribeChunks(playback.GetPlayer().GetLoadedSet());
}

namespace detail
{

// What a play tool's command line sets: the playback's options, the output file (-o) and --info.
template <typename Player>
struct PlayOptions
{
    player::PlaybackOptions<Player> playback;
    std::string out;
    bool info = false;
};

// Prints the help to `out`.
template <typename Player>
void PrintUsage(const PlayTool<Player>& tool, std::FILE* out)
{
    using Options = player::PlaybackOptions<Player>;
    std::fprintf(out,
                 "usage: %s [options] <file> [<file> ...]\n"
                 "\n"
                 "%s"
                 "\n"
                 "options:\n"
                 "  -o, --output FILE  %s\n"
                 "  --length T         play for T instead of the tagged length (seconds, m:s or h:m:s)\n"
                 "  --fade T           fade out over T instead of the tagged fade\n"
                 "  --default T        the length of a file without a length tag (default %lld:%02lld, with a %lld s\n"
                 "                     fade)\n"
                 "  --info             %s\n"
                 "  --version          show the version\n"
                 "  -h, --help         show this help\n",
                 tool.name.c_str(), tool.description.c_str(), tool.output_option.c_str(),
                 Options::kDefaultLengthMs / 60000, Options::kDefaultLengthMs / 1000 % 60,
                 Options::kDefaultFadeMs / 1000, tool.info_option.c_str());
}

// Prints the files that a set loads, its tags and its summary.
template <typename Player>
void PrintInfo(const PlayTool<Player>& tool, player::Playback<Player>& playback)
{
    const auto& set = playback.GetPlayer().GetLoadedSet();
    for (const auto& s : set.sources)
    {
        std::fprintf(stderr, "file: %s\n", s.c_str());
    }

    for (const auto& [k, v] : playback.GetTags())
    {
        std::fprintf(stderr, "%s=%s\n", k.c_str(), v.c_str());
    }

    const auto time = [](uint64_t frames)
    {
        return FramesToTime(frames, player::Playback<Player>::kSampleRate);
    };

    std::fprintf(stderr, "%s; length %s + fade %s%s\n", tool.describe_set(playback).c_str(),
                 time(playback.PlayFrames()).c_str(), time(playback.FadeFrames()).c_str(),
                 playback.HasLengthTag() ? "" : " (defaults)");
}

// Renders `in` to `out`. With --info, prints what `in` loads instead. Returns the exit code.
template <typename Player>
int Render(const PlayTool<Player>& tool, const std::string& in, const std::filesystem::path& out,
           const PlayOptions<Player>& o)
{
    using PlaybackType = player::Playback<Player>;

    // A render goes from the start to the end and never seeks. It takes no snapshots.
    player::PlaybackOptions<Player> options = o.playback;
    options.snapshots = false;

    PlaybackType playback;
    if (!playback.Open(in, psf::ReadWholeFile, options))
    {
        std::fprintf(stderr, "error: %s\n", playback.Error().c_str());
        return 1;
    }

    if (o.info)
    {
        PrintInfo(tool, playback);
        return 0;
    }

    // Reject a length that the file can't hold before the render. The render would take hours.
    if (const auto err = CheckAudioLength(out.string(), playback.LengthFrames()))
    {
        std::fprintf(stderr, "error: %s\n", err->c_str());
        return 1;
    }

    const auto t0 = std::chrono::steady_clock::now();

    if (!playback.Start())
    {
        std::fprintf(stderr, "error: %s\n", playback.Error().c_str());
        return 1;
    }

    const auto t1 = std::chrono::steady_clock::now();

    std::vector<int16_t> pcm;
    pcm.reserve(static_cast<std::size_t>(playback.LengthFrames()) * 2);
    std::vector<int16_t> block(2 * 4096);
    while (true)
    {
        const std::size_t n = playback.Render(block.data(), 4096);
        if (n == 0)
        {
            break;
        }

        pcm.insert(pcm.end(), block.begin(), block.begin() + static_cast<std::ptrdiff_t>(2 * n));
    }

    // After an emulation error, only the blocks before the one in which the error happened are written. The exit code
    // reports that the render failed.
    const bool failed = playback.GetPlayer().GetState() == Player::State::kError;
    if (failed)
    {
        std::fprintf(stderr, "error after %.2f s: %s\n",
                     playback.Position() / static_cast<double>(PlaybackType::kSampleRate), playback.Error().c_str());
        if (pcm.empty())
        {
            return 1;
        }
    }

    const auto t2 = std::chrono::steady_clock::now();
    const double boot = std::chrono::duration<double>(t1 - t0).count();
    const double secs = std::chrono::duration<double>(t2 - t1).count();

    if (const auto err = WriteAudio(out.string(), pcm, PlaybackType::kSampleRate))
    {
        std::fprintf(stderr, "error: %s\n", err->c_str());
        return 1;
    }

    // The macOS droplet shows the line before "output:" and opens the output's folder.
    const double audio = pcm.size() / 2.0 / PlaybackType::kSampleRate;
    std::fprintf(stderr, "%s: %.2f s rendered in %.2f s (%.2fx real time) after %.2f s of %s\n",
                 out.filename().string().c_str(), audio, secs, audio / secs, boot, tool.set_up.c_str());
    std::fprintf(stderr, "output: %s\n", std::filesystem::absolute(out).lexically_normal().string().c_str());

    return failed ? 1 : 0;
}

// Reads the options and inputs. Returns 0 to go on, -1 when there's nothing more to do (after --help or --version), or
// the exit code.
template <typename Player>
int ParseArgs(const PlayTool<Player>& tool, int argc, char** argv, PlayOptions<Player>& o,
              std::vector<std::string>& inputs)
{
    for (int i = 1; i < argc; i++)
    {
        const std::string a = argv[i];
        long long* time = nullptr;
        if (a == "-h" || a == "--help")
        {
            PrintUsage(tool, stdout);
            return -1;
        }
        else if (a == "--version")
        {
            std::printf("%s %s\n", tool.name.c_str(), tool.version.c_str());
            return -1;
        }
        else if (a == "-o" || a == "--output")
        {
            if (i + 1 >= argc)
            {
                std::fprintf(stderr, "error: %s %s\n", a.c_str(), tool.requires_a_value.c_str());
                return 2;
            }

            o.out = argv[++i];
        }
        else if (a == "--length")
        {
            time = &o.playback.length_override_ms;
        }
        else if (a == "--fade")
        {
            time = &o.playback.fade_override_ms;
        }
        else if (a == "--default")
        {
            time = &o.playback.default_length_ms;
        }
        else if (a == "--info")
        {
            o.info = true;
        }
        else if (a.size() > 1 && a[0] == '-')
        {
            std::fprintf(stderr, "error: unknown option %s (see --help)\n", a.c_str());
            return 2;
        }
        else
        {
            inputs.push_back(a);
        }

        if (time)
        {
            if (i + 1 >= argc)
            {
                std::fprintf(stderr, "error: %s %s\n", a.c_str(), tool.requires_a_value.c_str());
                return 2;
            }

            const std::string v = argv[++i];
            *time = psf::ParseTime(v);
            if (*time < 0)
            {
                std::fprintf(stderr, "error: bad time for %s: '%s'\n", a.c_str(), v.c_str());
                return 2;
            }
        }
    }

    if (inputs.empty())
    {
        PrintUsage(tool, stderr);
        return 2;
    }

    if (!o.out.empty() && inputs.size() > 1)
    {
        std::fprintf(stderr, "error: -o requires a single input file\n");
        return 2;
    }

    return 0;
}

template <typename Player>
int Run(const PlayTool<Player>& tool, int argc, char** argv)
{
    namespace fs = std::filesystem;

    PlayOptions<Player> o;
    std::vector<std::string> inputs;
    if (const int parsed = ParseArgs(tool, argc, argv, o, inputs))
    {
        return parsed < 0 ? 0 : parsed;
    }

    if (!InputsExist(inputs, "file"))
    {
        return 1;
    }

    if (tool.before_inputs)
    {
        tool.before_inputs();
    }

    // Renders one input. A folder fails. A library is skipped: the minis load it.
    const auto render = [&](const std::string& in)
    {
        std::error_code ec;
        if (fs::is_directory(in, ec))
        {
            std::fprintf(stderr, "error: %s is a folder, not %s\n", in.c_str(), tool.a_file.c_str());
            return 1;
        }

        if (AsciiLower(fs::path(in).extension().string()) == tool.library_extension)
        {
            std::fprintf(stderr, "skipped %s: a library that %s files load\n", in.c_str(), tool.mini_extension.c_str());
            return 0;
        }

        const fs::path out = o.out.empty() ? fs::path(in).replace_extension(".wav") : fs::path(o.out);
        return Render(tool, in, out, o);
    };
    return RunEachInput(inputs, render);
}

} // namespace detail

// Runs the play tool with the command line `argc` and `argv`, and returns its exit code. A format's main returns it.
template <typename Player>
int RunPlayTool(const PlayTool<Player>& tool, int argc, char** argv)
{
    return RunTool([&] { return detail::Run(tool, argc, argv); });
}

} // namespace xsflib::cli
