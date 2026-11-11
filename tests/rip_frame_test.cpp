// SPDX-License-Identifier: MIT

// Unit tests for the frame of the rip tools (xsflib/cli/rip.h): the options, a format's extra option, the errors, the
// loop over the inputs, the tags that every rip gets, the registries of file names, the skipped-sound text, the length
// analysis, the order of the analyses, their reports and the seconds that they took, the mini writer and its settings,
// the library writer, and RunParallel (xsflib/cli/parallel.h).

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <latch>
#include <limits>
#include <mutex>
#include <new>
#include <numeric>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "xsflib/cli/parallel.h"
#include "xsflib/cli/rip.h"
#include "xsflib/files.h"
#include "xsflib/rip/length.h"
#include "xsflib/testing/check.h"
#include "xsflib/testing/command_line.h"
#include "xsflib/testing/temp_folder.h"

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
            ripped.push_back(xsflib::Utf8(xsflib::PathFromUtf8(input).filename()));
            last = o;

            return input.ends_with("bad") ? 1 : 0;
        };
    }

    int Run(const std::vector<std::string>& args)
    {
        xsflib::testing::CommandLine command_line("tstrip", args);
        ripped.clear();

        return xsflib::cli::detail::RunRip(tool, command_line.Argc(), command_line.Argv());
    }

    xsflib::cli::RipTool<TestOptions> tool;
    std::vector<std::string> ripped;
    TestOptions last;
};

void TestOptionsAndInputs()
{
    const xsflib::testing::TempFolder temp("xsflib_rip_frame_test");
    const fs::path& dir = temp.Path();
    const std::string a = xsflib::Utf8(dir / "a");
    const std::string bad = xsflib::Utf8(dir / "bad");
    std::ofstream(dir / "a").put('a');
    std::ofstream(dir / "bad").put('b');
    TestTool t;

    XSFLIB_CHECK(t.Run({"--version"}) == 0 && t.ripped.empty());
    XSFLIB_CHECK(t.Run({}) == 2);
    XSFLIB_CHECK(t.Run({"--bogus", a}) == 2);
    XSFLIB_CHECK(t.Run({a, "--game"}) == 2);
    XSFLIB_CHECK(t.Run({"--jobs", "0", a}) == 2);
    XSFLIB_CHECK(t.Run({"--mode", "nope", a}) == 2);
    XSFLIB_CHECK(t.Run({"--only", "[", a}) == 2);
    XSFLIB_CHECK(t.Run({xsflib::Utf8(dir / "missing")}) == 1 && t.ripped.empty());

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

using xsflib::rip::Timing;

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
    const xsflib::testing::TempFolder temp("xsflib_rip_frame_minis");
    const fs::path& dir = temp.Path();
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
}

// The library writer makes the folder and writes the library under its name. A later rip with the same bytes keeps the
// name. A later rip with other bytes takes the next name, and the earlier library stays. A folder that can't be made
// throws.
void TestWriteLibrary()
{
    const xsflib::testing::TempFolder temp("xsflib_rip_frame_library");
    const fs::path dir = temp.Path() / "Game";
    const fs::path not_folder = temp.Path() / "file";
    const std::vector<uint8_t> bytes = {1, 2, 3};
    const std::vector<uint8_t> other = {4, 5, 6};
    xsflib::rip::FileNames first(".tstlib", ".minitst");
    xsflib::rip::FileNames same(".tstlib", ".minitst");
    xsflib::rip::FileNames changed(".tstlib", ".minitst");
    xsflib::rip::FileNames blocked(".tstlib", ".minitst");
    const bool file_written = !xsflib::WriteFile(not_folder, bytes);

    const std::optional<std::string> first_name = xsflib::cli::WriteLibrary(dir, first, "Game", bytes);
    const std::optional<std::string> same_name = xsflib::cli::WriteLibrary(dir, same, "Game", bytes);
    const std::optional<std::string> changed_name = xsflib::cli::WriteLibrary(dir, changed, "Game", other);
    const std::string error = xsflib::testing::ErrorOf<fs::filesystem_error>(
        [&] { xsflib::cli::WriteLibrary(not_folder / "sub", blocked, "Game", bytes); });

    std::vector<uint8_t> first_file;
    std::vector<uint8_t> changed_file;
    const bool read =
        xsflib::ReadFile(dir / "Game.tstlib", first_file) && xsflib::ReadFile(dir / "Game_2.tstlib", changed_file);

    XSFLIB_CHECK(first_name == "Game.tstlib" && same_name == "Game.tstlib" && changed_name == "Game_2.tstlib");
    XSFLIB_CHECK(read && first_file == bytes && changed_file == other);
    XSFLIB_CHECK(file_written && !error.empty());
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

// The mini writer's settings: no sound tag, the files' sizes in their lines, the paths of the files, and tags that the
// build adds.
void TestMiniSettings()
{
    const xsflib::testing::TempFolder temp("xsflib_rip_frame_mini_settings");
    const fs::path& dir = temp.Path();
    xsflib::rip::FileNames names(".tstlib", ".minitst");
    const std::vector<Job> jobs = {{1, "SEQ_1", true, Timing{61000, 10000}}, {2, "SEQ_2", true, std::nullopt}};
    const auto build = [](const Job& job, xsflib::psf::Tags& tags)
    {
        tags["track"] = std::to_string(job.index);
        const std::string area = xsflib::psf::FormatTagArea(tags);

        return std::vector<uint8_t>(area.begin(), area.end());
    };

    std::vector<fs::path> paths;
    int written = 0;
    const auto write = [&](std::FILE* file)
    {
        xsflib::cli::MiniSettings settings;
        settings.sizes = true;
        settings.paths = &paths;
        settings.stream = file;
        written = xsflib::cli::WriteMinis(dir, names, jobs, {{"game", "Game"}}, settings, build);
    };

    const std::vector<std::string> lines = LinesOf(write);

    // Each file holds the tag area of its tags, as the build made it.
    const std::string first = xsflib::psf::FormatTagArea(
        {{"game", "Game"}, {"title", "SEQ_1"}, {"length", "1:01.000"}, {"fade", "0:10.000"}, {"track", "1"}});
    const std::string second = xsflib::psf::FormatTagArea({{"game", "Game"}, {"title", "SEQ_2"}, {"track", "2"}});
    std::vector<uint8_t> first_file;
    std::vector<uint8_t> second_file;
    const bool read =
        paths.size() == 2 && xsflib::ReadFile(paths[0], first_file) && xsflib::ReadFile(paths[1], second_file);

    XSFLIB_CHECK(written == 2 && read);
    XSFLIB_CHECK(paths == std::vector<fs::path>({dir / "0001 SEQ_1.minitst", dir / "0002 SEQ_2.minitst"}));
    XSFLIB_CHECK(first_file == std::vector<uint8_t>(first.begin(), first.end()));
    XSFLIB_CHECK(second_file == std::vector<uint8_t>(second.begin(), second.end()));
    XSFLIB_CHECK(lines == std::vector<std::string>(
                              {"0001 SEQ_1.minitst  " + std::to_string(first.size()) + " bytes  length 1:01.000",
                               "0002 SEQ_2.minitst  " + std::to_string(second.size()) + " bytes"}));
}

// Analyzes every job. Those whose index is a multiple of 7 fail.
bool AnalyzeAllButSevens(Job& job)
{
    job.timing = job.index % 7 ? std::optional<Timing>(Timing{1000, 10000}) : std::nullopt;

    return true;
}

// The progress lines of an analysis of `jobs` with AnalyzeAllButSevens on four threads, with a line after every `every`
// jobs and after the last. The threads may print them out of order. Checks that the 36 of the 250 jobs that fail each
// get a warning.
std::set<std::string> ProgressLines(std::vector<Job>& jobs, std::size_t every)
{
    std::set<std::string> counts;
    std::size_t warnings = 0;
    const auto run = [&jobs, every](std::FILE* file)
    {
        xsflib::cli::AnalyzeLengths(jobs, 4, AnalyzeAllButSevens, {"", every, file});
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
}

void TestAnalysisSettings()
{
    std::vector<Job> jobs;
    for (uint32_t i = 0; i < 250; i++)
    {
        jobs.push_back({i, "SEQ_" + std::to_string(i), true, std::nullopt});
    }

    // The progress lines come after every `progress_every` jobs and after the last.
    XSFLIB_CHECK(ProgressLines(jobs, 100) ==
                 std::set<std::string>({"  analyzed 100 of 250", "  analyzed 200 of 250", "  analyzed 250 of 250"}));
    XSFLIB_CHECK(ProgressLines(jobs, 50) ==
                 std::set<std::string>({"  analyzed 50 of 250", "  analyzed 100 of 250", "  analyzed 150 of 250",
                                        "  analyzed 200 of 250", "  analyzed 250 of 250"}));

    // A format can name its pass in the progress lines.
    const auto named = [&jobs](std::FILE* file)
    {
        xsflib::cli::AnalyzeLengths(jobs, 4, AnalyzeAllButSevens, {"", 250, file, "found the waves of"});
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
        return xsflib::testing::ErrorOf<std::runtime_error>(
            [&] { xsflib::cli::AnalyzeLengths(jobs, 4, out_of_memory, settings); });
    };

    XSFLIB_CHECK(error_of({}) == "out of memory for the length analysis; try fewer --jobs");
    XSFLIB_CHECK(error_of({"out of memory during length analysis; try fewer --jobs"}) ==
                 "out of memory during length analysis; try fewer --jobs");
}

// With `seconds`, each job gets the seconds that its analysis took, analyzed or not. A job that doesn't run gets
// infinity.
void TestAnalysisSeconds()
{
    std::vector<Job> jobs;
    for (uint32_t i = 0; i < 20; i++)
    {
        jobs.push_back({i, "SEQ_" + std::to_string(i), i % 5 != 0, std::nullopt});
    }

    const auto analyze = [](Job& job)
    {
        job.timing = Timing{1000, 10000};

        return job.analyzable;
    };

    const auto finite = [](double s)
    {
        return std::isfinite(s) && s >= 0.0;
    };

    // The progress lines go to a temporary file.
    std::vector<double> seconds;
    const auto timed = [&jobs, &analyze, &seconds](std::FILE* file)
    {
        xsflib::cli::AnalysisSettings settings;
        settings.stream = file;
        settings.seconds = &seconds;
        xsflib::cli::AnalyzeLengths(jobs, 4, analyze, settings);
    };

    LinesOf(timed);

    XSFLIB_CHECK(seconds.size() == jobs.size());
    XSFLIB_CHECK(std::all_of(seconds.begin(), seconds.end(), finite));

    // With one thread, an analysis that runs out of memory on the first job stops before the second job.
    std::vector<Job> two = {jobs[1], jobs[2]};
    const auto out_of_memory = [](Job&) -> bool
    {
        throw std::bad_alloc();
    };

    std::vector<double> stopped;
    xsflib::cli::AnalysisSettings settings;
    settings.seconds = &stopped;

    const std::string error = xsflib::testing::ErrorOf<std::runtime_error>(
        [&] { xsflib::cli::AnalyzeLengths(two, 1, out_of_memory, settings); });

    XSFLIB_CHECK(!error.empty());
    XSFLIB_REQUIRE(stopped.size() == 2);
    XSFLIB_CHECK(std::isinf(stopped[0]) && std::isinf(stopped[1]));
}

// With more than one thread, the jobs start in order of decreasing cost. With one thread, the jobs start in the order
// given.
void TestAnalysisOrder()
{
    std::vector<Job> jobs;
    for (uint32_t i = 0; i < 6; i++)
    {
        jobs.push_back({i, "SEQ_" + std::to_string(i), true, std::nullopt});
    }

    // The indices of the jobs in the order in which they start. The first `threads` jobs wait for one another. Each
    // thread then runs one of the first jobs before any thread starts another.
    const auto starts = [&jobs](unsigned threads, const std::vector<double>& costs)
    {
        std::mutex mutex;
        std::vector<uint32_t> started;
        std::latch first(static_cast<std::ptrdiff_t>(threads));
        const auto analyze = [&mutex, &started, &first, threads](Job& job)
        {
            bool wait = false;
            {
                const std::lock_guard lock(mutex);
                started.push_back(job.index);
                wait = started.size() <= threads;
            }

            if (wait)
            {
                first.arrive_and_wait();
            }

            job.timing = Timing{1000, 10000};

            return true;
        };
        xsflib::cli::AnalysisSettings settings;
        settings.costs = costs;
        xsflib::cli::AnalyzeLengths(jobs, threads, analyze, settings);

        return started;
    };

    // The indices of the first two jobs to start, or none if fewer than two started.
    const auto first_two = [](const std::vector<uint32_t>& started)
    {
        return started.size() < 2 ? std::set<uint32_t>() : std::set<uint32_t>(started.begin(), started.begin() + 2);
    };

    constexpr double kUnknown = std::numeric_limits<double>::infinity();
    const std::vector<double> kCosts = {1.0, 5.0, kUnknown, 2.0, 5.0, 0.5};
    const std::vector<uint32_t> kInOrder = {0, 1, 2, 3, 4, 5};
    const std::set<uint32_t> kCostliest = {1, 2};
    const std::set<uint32_t> kFirstInOrder = {0, 1};

    XSFLIB_CHECK(starts(1, kCosts) == kInOrder);

    // The job of unknown cost and the first of the two costliest start first. Every job runs once.
    std::vector<uint32_t> two_threads = starts(2, kCosts);
    const std::set<uint32_t> first = first_two(two_threads);
    std::sort(two_threads.begin(), two_threads.end());

    XSFLIB_CHECK(first == kCostliest);
    XSFLIB_CHECK(two_threads == kInOrder);

    // Without costs, the jobs start in their order.
    XSFLIB_CHECK(first_two(starts(2, {})) == kFirstInOrder);
}

// The indices that RunParallel gave the work, sorted, and the threads that ran them.
struct ParallelRun
{
    std::vector<std::size_t> indices;
    std::set<std::thread::id> threads;
};

ParallelRun RunInParallel(std::size_t count, unsigned threads)
{
    std::mutex mutex;
    ParallelRun run;
    const auto work = [&mutex, &run](std::size_t i)
    {
        const std::lock_guard lock(mutex);
        run.indices.push_back(i);
        run.threads.insert(std::this_thread::get_id());
    };
    xsflib::cli::RunParallel(count, threads, work);

    std::sort(run.indices.begin(), run.indices.end());

    return run;
}

// The numbers from 0 up to `count`.
std::vector<std::size_t> Indices(std::size_t count)
{
    std::vector<std::size_t> indices(count);
    std::iota(indices.begin(), indices.end(), std::size_t{0});

    return indices;
}

// RunParallel runs each index once, on the caller's thread and up to `threads` - 1 more. A count of zero threads runs
// on the caller's thread alone. No more threads run than there are indices.
void TestRunParallel()
{
    const std::set<std::thread::id> caller = {std::this_thread::get_id()};

    const ParallelRun none = RunInParallel(0, 4);
    const ParallelRun one = RunInParallel(10, 1);
    const ParallelRun zero = RunInParallel(10, 0);
    const ParallelRun four = RunInParallel(100, 4);
    const ParallelRun few = RunInParallel(3, 8);

    XSFLIB_CHECK(none.indices.empty() && none.threads.empty());
    XSFLIB_CHECK(one.indices == Indices(10) && one.threads == caller);
    XSFLIB_CHECK(zero.indices == Indices(10) && zero.threads == caller);
    XSFLIB_CHECK(four.indices == Indices(100) && four.threads.size() <= 4);
    XSFLIB_CHECK(few.indices == Indices(3) && few.threads.size() <= 3);
}

// With four threads and four indices, the four run at once: each waits until all four have started. The caller's thread
// is one of them.
void TestRunParallelThreads()
{
    std::latch all(4);
    std::mutex mutex;
    std::set<std::thread::id> threads;
    const auto work = [&all, &mutex, &threads](std::size_t)
    {
        {
            const std::lock_guard lock(mutex);
            threads.insert(std::this_thread::get_id());
        }

        all.arrive_and_wait();
    };

    xsflib::cli::RunParallel(4, 4, work);

    XSFLIB_CHECK(threads.size() == 4 && threads.count(std::this_thread::get_id()) == 1);
}

// When the work throws, no further index starts, and RunParallel rethrows the first exception on the caller's thread.
void TestRunParallelFailure()
{
    std::vector<std::size_t> ran;
    const auto fail_at_3 = [&ran](std::size_t i)
    {
        ran.push_back(i);
        if (i == 3)
        {
            throw std::runtime_error("index 3 failed");
        }
    };

    const std::string error =
        xsflib::testing::ErrorOf<std::runtime_error>([&] { xsflib::cli::RunParallel(10, 1, fail_at_3); });

    XSFLIB_CHECK(error == "index 3 failed");
    XSFLIB_CHECK(ran == Indices(4));

    // An exception on another thread reaches the caller once the threads have stopped.
    const std::thread::id caller = std::this_thread::get_id();
    std::latch all(4);
    const auto fail_elsewhere = [&all, caller](std::size_t)
    {
        all.arrive_and_wait();
        if (std::this_thread::get_id() != caller)
        {
            throw std::runtime_error("failed on another thread");
        }
    };

    const std::string elsewhere =
        xsflib::testing::ErrorOf<std::runtime_error>([&] { xsflib::cli::RunParallel(4, 4, fail_elsewhere); });

    XSFLIB_CHECK(elsewhere == "failed on another thread");
}

} // namespace

int main()
{
    const auto tests = []
    {
        TestOptionsAndInputs();
        TestTags();
        TestSkippedText();
        TestNamesIn();
        TestAnalyzeAndWrite();
        TestMiniSettings();
        TestWriteLibrary();
        TestAnalysisSettings();
        TestAnalysisSeconds();
        TestAnalysisOrder();
        TestRunParallel();
        TestRunParallelThreads();
        TestRunParallelFailure();
    };

    return xsflib::testing::RunTest("rip frame", tests);
}
