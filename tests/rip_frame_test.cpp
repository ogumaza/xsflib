// SPDX-License-Identifier: MIT

// Unit tests for the frame of the rip tools (xsflib/cli/rip.h): the options, a format's extra option, the errors, the
// loop over the inputs, the tags that every rip gets, the registries of file names, the skipped-sound text, the length
// analysis and its reports, and the mini writer.

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <new>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "xsflib/cli/rip.h"
#include "xsflib/testing/check.h"

namespace
{

namespace fs = std::filesystem;

struct TestOptions : xsflib::cli::RipOptions
{
    std::string firmware;
};

// A tool with an extra option, --firmware. Its rips record the inputs they get.
struct TestTool
{
    TestTool()
    {
        tool.name = "tstrip";
        tool.version = "1.2.3";
        tool.usage = "usage: tstrip [options] <input>...\n";
        tool.requires_a_value = "needs a value";
        tool.max_jobs_32bit = 8;

        tool.parse_option = [](const std::string& option, const std::function<std::string()>& next, TestOptions& o)
        {
            if (option != "--firmware")
            {
                return -1;
            }

            o.firmware = next();

            return 0;
        };

        tool.rip_input = [this](const std::string& input, const TestOptions& o, xsflib::cli::FileNameRegistries&)
        {
            ripped.push_back(fs::path(input).filename().string());
            last = o;

            return input.ends_with("bad") ? 1 : 0;
        };
    }

    int Run(std::vector<std::string> args)
    {
        args.insert(args.begin(), "tstrip");
        std::vector<char*> argv;
        for (std::string& a : args)
        {
            argv.push_back(a.data());
        }

        ripped.clear();

        return xsflib::cli::detail::RunRip(tool, static_cast<int>(argv.size()), argv.data());
    }

    xsflib::cli::RipTool<TestOptions> tool;
    std::vector<std::string> ripped;
    TestOptions last;
};

void TestOptionsAndInputs()
{
    const fs::path dir = fs::temp_directory_path() / "xsflib_rip_frame_test";
    fs::create_directories(dir);
    const std::string a = (dir / "a").string();
    const std::string bad = (dir / "bad").string();
    std::ofstream(a).put('a');
    std::ofstream(bad).put('b');
    TestTool t;

    XSFLIB_CHECK(t.Run({"--version"}) == 0 && t.ripped.empty());
    XSFLIB_CHECK(t.Run({}) == 2);
    XSFLIB_CHECK(t.Run({"--bogus", a}) == 2);
    XSFLIB_CHECK(t.Run({a, "--game"}) == 2);
    XSFLIB_CHECK(t.Run({"--jobs", "0", a}) == 2);
    XSFLIB_CHECK(t.Run({"--mode", "nope", a}) == 2);
    XSFLIB_CHECK(t.Run({"--only", "[", a}) == 2);
    XSFLIB_CHECK(t.Run({(dir / "missing").string()}) == 1 && t.ripped.empty());

    // The options reach the rip, with --by's default, and the format's --firmware.
    XSFLIB_CHECK(t.Run({"--firmware", "f.bin", "--no-length", "--bgm", "--mode", "archive", "--archive", "x", "--only",
                        "^SEQ_", "--jobs", "3", a}) == 0);
    XSFLIB_CHECK(t.ripped == std::vector<std::string>({"a"}));
    XSFLIB_CHECK(t.last.firmware == "f.bin" && !t.last.lengths && t.last.bgm && t.last.mode == "archive");
    XSFLIB_CHECK(t.last.only_archive == "x" && t.last.only_text == "^SEQ_" && t.last.jobs == 3);
    XSFLIB_CHECK(t.last.by == "tstrip");

    // Each input once, the highest exit code.
    XSFLIB_CHECK(t.Run({a, bad, a}) == 1);
    XSFLIB_CHECK(t.ripped == std::vector<std::string>({"a", "bad"}));

    // A format without modes or --archive takes them as unknown options.
    t.tool.modes = false;
    t.tool.archives = false;

    XSFLIB_CHECK(t.Run({"--mode", "game", a}) == 2);
    XSFLIB_CHECK(t.Run({"--archive", "x", a}) == 2);

    fs::remove_all(dir);
}

void TestTags()
{
    xsflib::cli::RipOptions o;
    o.by = "tstrip";
    o.artist = "Someone";

    const auto tags = xsflib::cli::RipTags(o, "Game", "tst", "archive");

    XSFLIB_CHECK(tags.at("game") == "Game" && tags.at("artist") == "Someone" && !tags.count("year"));
    XSFLIB_CHECK(tags.at("tstby") == "tstrip" && tags.at("tst_mode") == "archive" && tags.at("utf8") == "1");
    XSFLIB_CHECK(!xsflib::cli::RipTags(o, "Game", "tst", nullptr).count("tst_mode"));
}

void TestSkippedText()
{
    using xsflib::cli::SkippedText;

    XSFLIB_CHECK(SkippedText(0, "data outside the archive", 0, "sound").empty());
    XSFLIB_CHECK(SkippedText(1, "data outside the archive", 0, "sound") ==
                 "; 1 sound skipped (data outside the archive)");
    XSFLIB_CHECK(SkippedText(0, "data not there", 2, "sequence") == "; 2 sequences skipped (no sound)");
    XSFLIB_CHECK(SkippedText(2, "data outside the archive", 1, "sound") ==
                 "; 3 sounds skipped (2 data outside the archive, 1 no sound)");

    // With an empty noun, the count stands alone.
    XSFLIB_CHECK(SkippedText(0, "", 3, "") == "; 3 skipped (no sound)");
    XSFLIB_CHECK(SkippedText(0, "", 1, "") == "; 1 skipped (no sound)");
}

void TestNamesIn()
{
    const fs::path dir = fs::temp_directory_path() / "xsflib_rip_frame_names";
    xsflib::cli::FileNameRegistries registries;

    // Folders whose names differ only in case share one registry, with the extensions of the first.
    xsflib::rip::FileNames& upper = xsflib::cli::NamesIn(registries, dir / "Out", ".tstlib", ".minitst");
    xsflib::rip::FileNames& lower = xsflib::cli::NamesIn(registries, dir / "out", ".otherlib", ".miniother");
    xsflib::rip::FileNames& other = xsflib::cli::NamesIn(registries, dir / "other", ".tstlib", ".minitst");

    XSFLIB_CHECK(&upper == &lower && &upper != &other && registries.size() == 2);
    XSFLIB_CHECK(upper.Mini(1, "SEQ_1") == "0001 SEQ_1.minitst");
}

struct Timing
{
    long long length_ms;
    long long fade_ms;
};

struct Job
{
    uint32_t index;
    std::string label;
    bool analyzable;
    std::optional<Timing> timing;
};

void TestAnalyzeAndWrite()
{
    std::vector<Job> jobs;
    for (uint32_t i = 0; i < 250; i++)
    {
        jobs.push_back({i, "SEQ_" + std::to_string(i), i % 5 != 0, std::nullopt});
    }

    // Every analyzable job gets a timing, except those whose analysis fails.
    const auto analyze = [](Job& job)
    {
        if (!job.analyzable)
        {
            return false;
        }

        if (job.index % 7 != 0)
        {
            job.timing = Timing{1000 * job.index, 10000};
        }

        return true;
    };

    xsflib::cli::AnalyzeLengths(jobs, 4, analyze);

    int timed = 0;
    for (const Job& job : jobs)
    {
        timed += job.timing.has_value();
        XSFLIB_CHECK(job.timing.has_value() == (job.analyzable && job.index % 7 != 0));
    }

    XSFLIB_CHECK(timed > 0);

    // A mini file for each job, with the common tags and the job's label as its title and sound tag.
    const fs::path dir = fs::temp_directory_path() / "xsflib_rip_frame_minis";
    fs::remove_all(dir);
    fs::create_directories(dir);
    xsflib::rip::FileNames names(".tstlib", ".minitst");
    const std::vector<Job> two = {jobs[1], jobs[5]};
    xsflib::psf::Tags seen;
    const auto build = [&seen](const Job&, const xsflib::psf::Tags& tags)
    {
        seen = tags;

        return std::vector<uint8_t>{1, 2, 3};
    };

    const int written = xsflib::cli::WriteMinis(dir, names, two, {{"game", "Game"}}, "tst_sound", build);

    XSFLIB_CHECK(written == 2);
    XSFLIB_CHECK(fs::exists(dir / "0001 SEQ_1.minitst") && fs::exists(dir / "0005 SEQ_5.minitst"));
    XSFLIB_CHECK(seen.at("title") == "SEQ_5" && seen.at("tst_sound") == "SEQ_5" && seen.at("game") == "Game");
    XSFLIB_CHECK(!seen.count("length"));

    fs::remove_all(dir);
}

// Returns the lines that `run` writes to a temporary file.
template <typename Run>
std::vector<std::string> LinesOf(const Run& run)
{
    std::FILE* file = std::tmpfile();
    if (!file)
    {
        return {"no temporary file"};
    }

    run(file);
    std::rewind(file);

    std::vector<std::string> lines;
    std::string line;
    for (int c; (c = std::fgetc(file)) != EOF;)
    {
        if (c == '\n')
        {
            lines.push_back(line);
            line.clear();
        }
        else
        {
            line.push_back(static_cast<char>(c));
        }
    }

    std::fclose(file);

    return lines;
}

void TestAnalysisSettings()
{
    std::vector<Job> jobs;
    for (uint32_t i = 0; i < 250; i++)
    {
        jobs.push_back({i, "SEQ_" + std::to_string(i), true, std::nullopt});
    }

    // Every job is analyzed. Those whose index is a multiple of 7 fail.
    const auto analyze = [](Job& job)
    {
        job.timing = job.index % 7 ? std::optional<Timing>(Timing{1000, 10000}) : std::nullopt;

        return true;
    };

    // The progress lines come after every `progress_every` jobs and after the last. The threads may print them out of
    // order.
    const auto progress = [&jobs, &analyze](std::size_t every)
    {
        std::set<std::string> counts;
        std::size_t warnings = 0;
        const auto run = [&jobs, &analyze, every](std::FILE* file)
        {
            xsflib::cli::AnalyzeLengths(jobs, 4, analyze, {"", every, file});
        };
        for (const std::string& line : LinesOf(run))
        {
            if (line.starts_with("  analyzed "))
            {
                counts.insert(line);
            }
            else
            {
                warnings += line.starts_with("warning: length analysis failed for SEQ_");
            }
        }

        XSFLIB_CHECK(warnings == 36);

        return counts;
    };

    XSFLIB_CHECK(progress(100) ==
                 std::set<std::string>({"  analyzed 100 of 250", "  analyzed 200 of 250", "  analyzed 250 of 250"}));
    XSFLIB_CHECK(progress(50) ==
                 std::set<std::string>({"  analyzed 50 of 250", "  analyzed 100 of 250", "  analyzed 150 of 250",
                                        "  analyzed 200 of 250", "  analyzed 250 of 250"}));

    // A format can name its pass in the progress lines.
    const auto named = [&jobs, &analyze](std::FILE* file)
    {
        xsflib::cli::AnalyzeLengths(jobs, 4, analyze, {"", 250, file, "found the waves of"});
    };

    std::set<std::string> named_lines;
    for (const std::string& line : LinesOf(named))
    {
        if (line.starts_with("  found the waves of "))
        {
            named_lines.insert(line);
        }
    }

    XSFLIB_CHECK(named_lines == std::set<std::string>({"  found the waves of 250 of 250"}));

    // An analysis that runs out of memory stops them all, with the default error or the format's error.
    const auto out_of_memory = [](Job& job)
    {
        if (job.index == 3)
        {
            throw std::bad_alloc();
        }

        return true;
    };

    const auto error_of = [&jobs, &out_of_memory](const xsflib::cli::AnalysisSettings& settings)
    {
        try
        {
            xsflib::cli::AnalyzeLengths(jobs, 4, out_of_memory, settings);
        }
        catch (const std::runtime_error& e)
        {
            return std::string(e.what());
        }

        return std::string("no error");
    };

    XSFLIB_CHECK(error_of({}) == "out of memory for the length analysis; try fewer --jobs");
    XSFLIB_CHECK(error_of({"out of memory during length analysis; try fewer --jobs"}) ==
                 "out of memory during length analysis; try fewer --jobs");
}

} // namespace

int main()
{
    TestOptionsAndInputs();
    TestTags();
    TestSkippedText();
    TestNamesIn();
    TestAnalyzeAndWrite();
    TestAnalysisSettings();

    return xsflib::testing::Summary("rip frame");
}
