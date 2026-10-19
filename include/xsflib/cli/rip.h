// SPDX-License-Identifier: MIT

// The frame of a format's rip tool: the options that every rip tool has, the check of the inputs, the loop over them
// and main. The options:
//
//   -o, --output DIR      the folder the rips go in
//   --list                list what the inputs hold, then stop
//   --mode game|archive   the rip mode, for a format with both modes
//   --archive PATH        rip only this sound archive of a game, for a format with --archive
//   --only REGEX          rip only the sounds whose label matches (ECMAScript regex)
//   --bgm                 rip only the music
//   --no-length           don't analyze lengths
//   --jobs N              length analyses run in parallel (default: the number of CPU threads)
//   --game NAME, --artist NAME, --year YEAR, --copyright TEXT, --by NAME
//   --version             print the version
//   -h, --help            print the format's usage
//
// The inputs are ripped one after another. An input that fails doesn't stop the rest. Each format gives its names, its
// help, the wording of some messages and its other options in a RipTool, and rips each input itself. The format's main
// calls RunRipTool.

#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <functional>
#include <map>
#include <new>
#include <optional>
#include <regex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "xsflib/ascii.h"
#include "xsflib/cli/tool.h"
#include "xsflib/files.h"
#include "xsflib/psf/psf.h"
#include "xsflib/rip/file_names.h"
#include "xsflib/rip/tool.h"

namespace xsflib::cli
{

// The options that every rip tool has. A format's options derive from it.
struct RipOptions
{
    std::regex only{".*"};
    std::string only_text; // the pattern --only gave, or empty
    std::string out_dir, mode, only_archive, game_name, artist, year, copyright, by;
    bool list = false, lengths = true, bgm = false;
    unsigned jobs = 0;
};

// The file names of each output folder, by the folder's lower-cased absolute path. Rips in the same folder share them.
// Then no rip overwrites another rip's files.
using FileNameRegistries = std::map<std::string, rip::FileNames>;

// The registry of file names for the rips in `folder`, with the format's extensions. Rips in one folder share it. Then
// no rip overwrites another rip's files. Folders whose names differ only in case count as one: Windows and macOS treat
// them as one folder.
inline rip::FileNames& NamesIn(FileNameRegistries& registries, const std::filesystem::path& folder,
                               const std::string& library_extension, const std::string& mini_extension)
{
    return registries
        .try_emplace(AsciiLower(std::filesystem::absolute(folder).lexically_normal().string()), library_extension,
                     mini_extension)
        .first->second;
}

// What differs between the formats' rip tools: the names, the help, some words of the messages, the other options and
// the rip of an input.
template <typename Options>
struct RipTool
{
    // The tool's name ("xyzrip") and version. The name is also --by's default.
    std::string name;
    std::string version;

    // The help, printed as it is.
    const char* usage = "";

    // The words in "error: --game requires a value".
    std::string requires_a_value = "requires a value";

    // Whether the format has --mode game|archive, and the error for another mode.
    bool modes = true;
    std::string mode_error = "--mode must be game or archive";

    // Whether the format has --archive PATH.
    bool archives = true;

    // The most length analyses that a 32-bit build runs at once, if the format limits them.
    std::optional<unsigned> max_jobs_32bit;

    // Takes one of the format's other options. `next` reads the option's value. Returns -1 if `option` isn't one of
    // them, 0 when it took the option, or 2 after printing an error.
    std::function<int(const std::string& option, const std::function<std::string()>& next, Options& o)> parse_option;

    // Runs once before the first input, after the inputs are checked. It may take some inputs out. Returns 0 to go on,
    // or the exit code.
    std::function<int(std::vector<std::string>& inputs, Options& o)> before_inputs;

    // Rips one input, with the file names of the folders written so far. Returns its exit code.
    std::function<int(const std::string& input, const Options& o, FileNameRegistries& names)> rip_input;
};

// The tags that every rip of the format gets: the game and the tags given on the command line, the ripper's name as
// <format>by (such as xyzby), the mode as <format>_mode when `mode` isn't null, and utf8.
inline psf::Tags RipTags(const RipOptions& o, const std::string& game_name, const std::string& format, const char* mode)
{
    psf::Tags common;
    common["game"] = game_name;

    if (!o.artist.empty())
    {
        common["artist"] = o.artist;
    }

    if (!o.year.empty())
    {
        common["year"] = o.year;
    }

    if (!o.copyright.empty())
    {
        common["copyright"] = o.copyright;
    }

    common[format + "by"] = o.by;
    if (mode != nullptr)
    {
        common[format + "_mode"] = mode;
    }

    common["utf8"] = "1"; // tags are UTF-8 (PSF convention)

    return common;
}

// "; 3 sounds skipped (2 data outside the archive, 1 no sound)", or nothing when none were skipped. `noun` names what
// was skipped ("sound" or "sequence"), and `why` why the first `missing` of them were. With an empty noun, the count
// stands alone: "; 3 skipped (no sound)".
inline std::string SkippedText(std::size_t missing, const std::string& why, std::size_t silent, const std::string& noun)
{
    const std::size_t total = missing + silent;
    if (total == 0)
    {
        return "";
    }

    std::string reasons = missing ? why : "no sound";
    if (missing && silent)
    {
        reasons = std::to_string(missing) + " " + why + ", " + std::to_string(silent) + " no sound";
    }

    const std::string what = noun.empty() ? "" : " " + noun + (total == 1 ? "" : "s");

    return "; " + std::to_string(total) + what + " skipped (" + reasons + ")";
}

// How AnalyzeLengths reports.
struct AnalysisSettings
{
    // The error that AnalyzeLengths throws when an analysis runs out of memory.
    std::string out_of_memory = "out of memory for the length analysis; try fewer --jobs";

    // A progress line, such as "  analyzed 100 of 250", after every this many jobs and after the last.
    std::size_t progress_every = 100;

    // The stream for the warnings and the progress lines.
    std::FILE* stream = stderr;

    // The words of a progress line before its counts: "  <progress> 100 of 250".
    std::string progress = "analyzed";
};

// Length analysis of each job, spread over `threads` threads. `analyze(job)` analyzes one job, sets its `timing` and
// returns true, or returns false for a job that isn't analyzed. A job has a `label` and an optional `timing`. `analyze`
// reports a failed analysis by returning true with `timing` left empty. AnalyzeLengths then prints a warning for the
// job. `analyze` mustn't throw anything but std::bad_alloc. An analysis that runs out of memory stops them all and
// throws.
template <typename Job, typename Analyze>
void AnalyzeLengths(std::vector<Job>& jobs, unsigned threads, const Analyze& analyze,
                    const AnalysisSettings& settings = {})
{
    std::atomic<std::size_t> next{0};
    std::atomic<std::size_t> done{0};
    std::atomic<bool> out_of_memory{false};
    const auto worker = [&]
    {
        for (std::size_t i; (i = next++) < jobs.size();)
        {
            bool analyzed = false;
            try
            {
                analyzed = analyze(jobs[i]);
            }
            catch (const std::bad_alloc&)
            {
                out_of_memory = true;
                next = jobs.size(); // the other threads take no more jobs
                return;
            }

            if (analyzed && !jobs[i].timing)
            {
                std::fprintf(settings.stream, "warning: length analysis failed for %s; no length tag written\n",
                             jobs[i].label.c_str());
            }

            const std::size_t n = ++done;
            if (n % settings.progress_every == 0 || n == jobs.size())
            {
                std::fprintf(settings.stream, "  %s %zu of %zu\n", settings.progress.c_str(), n, jobs.size());
            }
        }
    };

    threads = std::max(1u, std::min<unsigned>(threads, static_cast<unsigned>(jobs.size())));
    std::vector<std::thread> pool;
    for (unsigned t = 1; t < threads; t++)
    {
        // If another thread can't start (the system is out of threads or memory), the ones already going do the work.
        try
        {
            pool.emplace_back(worker);
        }
        catch (const std::exception&)
        {
            break;
        }
    }

    worker();

    for (auto& t : pool)
    {
        t.join();
    }

    if (out_of_memory)
    {
        throw std::runtime_error(settings.out_of_memory);
    }
}

// Writes a mini file for each job into `out_dir`, named by `names`, with the `common` tags, the job's label as its
// title and as the tag `sound_tag`, and its length and fade when it has a timing. `build(job, tags)` makes the file.
// Prints a line for each file. Returns the number of files written, or -1 after printing an error.
template <typename Job, typename Build>
int WriteMinis(const std::filesystem::path& out_dir, rip::FileNames& names, const std::vector<Job>& jobs,
               const psf::Tags& common, const std::string& sound_tag, const Build& build)
{
    int count = 0;
    for (const Job& job : jobs)
    {
        psf::Tags tags = common;
        tags["title"] = job.label;
        tags[sound_tag] = job.label;
        if (job.timing)
        {
            tags["length"] = psf::FormatTime(job.timing->length_ms);
            tags["fade"] = psf::FormatTime(job.timing->fade_ms);
        }

        const std::string name = names.Mini(job.index, job.label);
        if (!rip::WriteFile(out_dir / PathFromUtf8(name), build(job, tags)))
        {
            std::fprintf(stderr, "error: can't write %s\n", name.c_str());
            return -1;
        }

        count++;
        std::fprintf(stderr, "%s%s%s\n", name.c_str(), tags.count("length") ? "  length " : "",
                     tags.count("length") ? tags["length"].c_str() : "");
    }

    return count;
}

namespace detail
{

// Reads the options and inputs. Returns 0 to go on, -1 when there's nothing more to do (after --help or --version), or
// the exit code.
template <typename Options>
int ParseRipArgs(const RipTool<Options>& tool, int argc, char** argv, Options& o, std::vector<std::string>& inputs)
{
    std::string only;
    for (int i = 1; i < argc; i++)
    {
        const std::string a = argv[i];

        bool missing_value = false;
        const std::function<std::string()> next = [&]() -> std::string
        {
            if (i + 1 >= argc)
            {
                missing_value = true;
                return "";
            }

            return argv[++i];
        };

        if (a == "-h" || a == "--help")
        {
            std::fputs(tool.usage, stdout);
            return -1;
        }
        else if (a == "--version")
        {
            std::printf("%s %s\n", tool.name.c_str(), tool.version.c_str());
            return -1;
        }
        else if (a == "-o" || a == "--output")
        {
            o.out_dir = next();
        }
        else if (a == "--list")
        {
            o.list = true;
        }
        else if (tool.modes && a == "--mode")
        {
            o.mode = next();
        }
        else if (tool.archives && a == "--archive")
        {
            o.only_archive = next();
        }
        else if (a == "--only")
        {
            only = next();
        }
        else if (a == "--bgm")
        {
            o.bgm = true;
        }
        else if (a == "--no-length")
        {
            o.lengths = false;
        }
        else if (a == "--jobs")
        {
            const std::string v = next();
            char* end = nullptr;
            const long n = std::strtol(v.c_str(), &end, 10);
            if (v.empty() || *end || n < 1 || n > 1024)
            {
                std::fprintf(stderr, "error: --jobs takes a number of threads from 1 to 1024, not '%s'\n", v.c_str());
                return 2;
            }

            o.jobs = static_cast<unsigned>(n);
        }
        else if (a == "--game")
        {
            o.game_name = next();
        }
        else if (a == "--artist")
        {
            o.artist = next();
        }
        else if (a == "--year")
        {
            o.year = next();
        }
        else if (a == "--copyright")
        {
            o.copyright = next();
        }
        else if (a == "--by")
        {
            o.by = next();
        }
        else if (const int taken = tool.parse_option ? tool.parse_option(a, next, o) : -1; taken >= 0)
        {
            if (taken > 0)
            {
                return taken;
            }
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

        if (missing_value)
        {
            std::fprintf(stderr, "error: %s %s\n", a.c_str(), tool.requires_a_value.c_str());
            return 2;
        }
    }

    if (inputs.empty())
    {
        std::fputs(tool.usage, stderr);
        return 2;
    }

    if (tool.modes && !o.mode.empty() && o.mode != "game" && o.mode != "archive")
    {
        std::fprintf(stderr, "error: %s\n", tool.mode_error.c_str());
        return 2;
    }

    // A pattern that doesn't compile is reported now, before anything is written.
    if (!only.empty())
    {
        try
        {
            o.only = std::regex(only);
            o.only_text = only;
        }
        catch (const std::regex_error& e)
        {
            std::fprintf(stderr, "error: --only: %s\n", e.what());
            return 2;
        }
    }

    if (o.jobs == 0)
    {
        o.jobs = std::max(1u, std::thread::hardware_concurrency());
        if constexpr (sizeof(void*) < 8)
        {
            if (tool.max_jobs_32bit)
            {
                o.jobs = std::min(o.jobs, *tool.max_jobs_32bit);
            }
        }
    }

    return 0;
}

template <typename Options>
int RunRip(const RipTool<Options>& tool, int argc, char** argv)
{
    Options o;
    o.by = tool.name;
    std::vector<std::string> inputs;
    if (const int parsed = ParseRipArgs(tool, argc, argv, o, inputs))
    {
        return parsed < 0 ? 0 : parsed;
    }

    if (!InputsExist(inputs, "folder"))
    {
        return 1;
    }

    if (tool.before_inputs)
    {
        if (const int result = tool.before_inputs(inputs, o))
        {
            return result;
        }
    }

    // The rips share the file names of the folders written so far. An input whose canonical path can't be found counts
    // by its absolute path without a trailing separator.
    FileNameRegistries names;
    return RunEachInput(inputs, rip::InputPath, [&](const std::string& in) { return tool.rip_input(in, o, names); });
}

} // namespace detail

// Runs the rip tool with the command line `argc` and `argv`, and returns its exit code. A format's main returns it.
template <typename Options>
int RunRipTool(const RipTool<Options>& tool, int argc, char** argv)
{
    return RunTool([&] { return detail::RunRip(tool, argc, argv); });
}

} // namespace xsflib::cli
