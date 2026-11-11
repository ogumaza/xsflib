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
// calls RunRipTool. A rip tool analyzes the lengths of its sounds with AnalyzeLengths (length_analysis.h).

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <regex>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#include "xsflib/ascii.h"
#include "xsflib/cli/length_analysis.h"
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

// The registry of file names for the rips in `folder`, with the format's extensions. Folders whose names differ only in
// case count as one: Windows and macOS treat them as one folder.
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

// Writes the library `bytes` into `out_dir`, which it creates if it doesn't exist, under the name that `names` gives it
// from `base` (see FileNames::LibraryIn). An earlier rip's library there stays. Prints a line for the file. Returns the
// library's name, or nothing after printing an error. Throws std::filesystem::filesystem_error if the folder can't be
// created.
inline std::optional<std::string> WriteLibrary(const std::filesystem::path& out_dir, rip::FileNames& names,
                                               const std::string& base, const std::vector<uint8_t>& bytes)
{
    const std::string name = names.LibraryIn(out_dir, base, bytes);
    std::filesystem::create_directories(out_dir);
    if (!rip::WriteFile(out_dir / PathFromUtf8(name), bytes))
    {
        std::fprintf(stderr, "error: can't write %s\n", name.c_str());
        return std::nullopt;
    }

    std::fprintf(stderr, "wrote %s (%zu bytes)\n", name.c_str(), bytes.size());

    return name;
}

// How WriteMinis tags and reports the minis.
struct MiniSettings
{
    // The tag that gets each job's label, such as "xyz_sound", or empty for none.
    std::string sound_tag;

    // Whether each file's line gives the file's size after its name: "0001 Title.minixyz  1234 bytes".
    bool sizes = false;

    // If not null, gets the path of each file written, in the order of the jobs.
    std::vector<std::filesystem::path>* paths = nullptr;

    // The stream for the lines and the error.
    std::FILE* stream = stderr;
};

// Writes a mini file for each job into `out_dir`, named by `names`, with the `common` tags, the job's label as its
// title and as the tag `settings.sound_tag`, and its length and fade when it has a timing. A job has an `index`, the
// number in its file's name, a `label` and an optional `timing`. `build(job, tags)` makes the file. It may add tags to
// `tags` first. Prints a line for each file to `settings.stream`. Returns the number of files written, or -1 after
// printing an error.
template <typename Job, typename Build>
int WriteMinis(const std::filesystem::path& out_dir, rip::FileNames& names, const std::vector<Job>& jobs,
               const psf::Tags& common, const MiniSettings& settings, const Build& build)
{
    int count = 0;
    for (const Job& job : jobs)
    {
        psf::Tags tags = common;
        tags["title"] = job.label;
        if (!settings.sound_tag.empty())
        {
            tags[settings.sound_tag] = job.label;
        }

        std::string length;
        if (job.timing)
        {
            length = psf::FormatTime(job.timing->length_ms);
            tags["length"] = length;
            tags["fade"] = psf::FormatTime(job.timing->fade_ms);
        }

        const std::string name = names.Mini(job.index, job.label);
        const std::filesystem::path path = out_dir / PathFromUtf8(name);
        const std::vector<uint8_t> mini = build(job, tags);
        if (!rip::WriteFile(path, mini))
        {
            std::fprintf(settings.stream, "error: can't write %s\n", name.c_str());
            return -1;
        }

        count++;
        if (settings.paths)
        {
            settings.paths->push_back(path);
        }

        const char* length_label = length.empty() ? "" : "  length ";
        if (settings.sizes)
        {
            std::fprintf(settings.stream, "%s  %zu bytes%s%s\n", name.c_str(), mini.size(), length_label,
                         length.c_str());
        }
        else
        {
            std::fprintf(settings.stream, "%s%s%s\n", name.c_str(), length_label, length.c_str());
        }
    }

    return count;
}

// WriteMinis with the tag `sound_tag`, and lines without sizes on stderr.
template <typename Job, typename Build>
int WriteMinis(const std::filesystem::path& out_dir, rip::FileNames& names, const std::vector<Job>& jobs,
               const psf::Tags& common, const std::string& sound_tag, const Build& build)
{
    MiniSettings settings;
    settings.sound_tag = sound_tag;

    return WriteMinis(out_dir, names, jobs, common, settings, build);
}

namespace detail
{

// Takes one of the options that every rip tool has, or one of the format's (RipTool::parse_option). `only` gets the
// pattern of --only. Returns -1 if `a` isn't one of them, 0 when it took `a`, or the exit code after printing an error.
template <typename Options>
int TakeRipOption(const RipTool<Options>& tool, const std::string& a, OptionValue& value, Options& o, std::string& only)
{
    // The options that take text, and what each sets.
    const std::pair<const char*, std::string*> text_options[] = {{"-o", &o.out_dir},
                                                                 {"--output", &o.out_dir},
                                                                 {"--only", &only},
                                                                 {"--game", &o.game_name},
                                                                 {"--artist", &o.artist},
                                                                 {"--year", &o.year},
                                                                 {"--copyright", &o.copyright},
                                                                 {"--by", &o.by}};
    for (const auto& [name, field] : text_options)
    {
        if (a == name)
        {
            *field = value();
            return 0;
        }
    }

    if (tool.modes && a == "--mode")
    {
        o.mode = value();
        return 0;
    }

    if (tool.archives && a == "--archive")
    {
        o.only_archive = value();
        return 0;
    }

    // The options that set a flag, and the value that each gives it.
    const std::tuple<const char*, bool*, bool> flag_options[] = {
        {"--list", &o.list, true}, {"--bgm", &o.bgm, true}, {"--no-length", &o.lengths, false}};
    for (const auto& [name, flag, set] : flag_options)
    {
        if (a == name)
        {
            *flag = set;
            return 0;
        }
    }

    if (a == "--jobs")
    {
        const std::string v = value();
        char* end = nullptr;
        const long n = std::strtol(v.c_str(), &end, 10);
        if (v.empty() || *end || n < 1 || n > 1024)
        {
            std::fprintf(stderr, "error: --jobs takes a number of threads from 1 to 1024, not '%s'\n", v.c_str());
            return 2;
        }

        o.jobs = static_cast<unsigned>(n);
        return 0;
    }

    if (!tool.parse_option)
    {
        return -1;
    }

    const std::function<std::string()> next = [&value]
    {
        return value();
    };

    return tool.parse_option(a, next, o);
}

// Checks the options once the command line is read, and fills in the default number of jobs. `only` is the pattern of
// --only. Returns 0 to go on, or 2 after printing an error.
template <typename Options>
int CheckRipOptions(const RipTool<Options>& tool, const std::string& only, Options& o)
{
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

// Reads the options and inputs. Returns 0 to go on, -1 when there's nothing more to do (after --help or --version), or
// the exit code.
template <typename Options>
int ParseRipArgs(const RipTool<Options>& tool, int argc, char** argv, Options& o, std::vector<std::string>& inputs)
{
    std::string only;
    const auto usage = [&tool](std::FILE* out)
    {
        std::fputs(tool.usage, out);
    };

    const auto option = [&](const std::string& a, OptionValue& value)
    {
        return TakeRipOption(tool, a, value, o, only);
    };

    if (const int parsed = ParseArguments(tool, argc, argv, usage, option, inputs))
    {
        return parsed;
    }

    return CheckRipOptions(tool, only, o);
}

template <typename Options>
int RunRip(const RipTool<Options>& tool, int argc, char** argv)
{
    Options o;
    o.by = tool.name;
    const auto parse = [&](std::vector<std::string>& inputs)
    {
        return ParseRipArgs(tool, argc, argv, o, inputs);
    };

    const auto before = [&](std::vector<std::string>& inputs)
    {
        return tool.before_inputs ? tool.before_inputs(inputs, o) : 0;
    };

    // The rips share the file names of the folders written so far.
    FileNameRegistries names;
    const auto rip_input = [&](const std::string& in)
    {
        return tool.rip_input(in, o, names);
    };

    return RunInputs(parse, "folder", before, rip_input);
}

} // namespace detail

// Runs the rip tool with the command line `argc` and `argv`, and returns its exit code. A format's main returns it.
template <typename Options>
int RunRipTool(const RipTool<Options>& tool, int argc, char** argv)
{
    return RunTool([&] { return detail::RunRip(tool, argc, argv); });
}

} // namespace xsflib::cli
