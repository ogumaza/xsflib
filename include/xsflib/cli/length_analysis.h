// SPDX-License-Identifier: MIT

// The length analysis of a rip tool's jobs, spread over threads (see rip.h): AnalyzeLengths and its settings.

#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <limits>
#include <new>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

#include "xsflib/cli/parallel.h"

namespace xsflib::cli
{
namespace detail
{

// The indices of `count` jobs, in the order in which they start. With more than one thread, and with a cost for each
// job, they start by decreasing cost (see AnalysisSettings::costs). Otherwise they start in their order.
inline std::vector<std::size_t> StartOrder(std::size_t count, unsigned threads, const std::vector<double>& costs)
{
    std::vector<std::size_t> order(count);
    std::iota(order.begin(), order.end(), std::size_t{0});
    if (threads > 1 && costs.size() == count)
    {
        const auto costlier = [&costs](std::size_t a, std::size_t b)
        {
            return costs[a] > costs[b];
        };
        std::stable_sort(order.begin(), order.end(), costlier);
    }

    return order;
}

} // namespace detail

// How AnalyzeLengths reports. The settings also set the order in which the jobs start, and can record the time that
// each job took.
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

    // The expected cost of each job, in the order of the jobs, or empty. The costs can come from an earlier pass over
    // the same sounds: the seconds that each sound took there, for example. With more than one thread, the jobs start
    // in order of decreasing cost. Jobs of equal cost keep their order. The costliest jobs then don't run alone at the
    // end. Give a job whose cost isn't known a cost of infinity. That job then starts first. A cost mustn't be NaN.
    // With one thread, or with costs for another number of jobs, the jobs start in their order.
    std::vector<double> costs = {};

    // If not null, gets the seconds that `analyze` took for each job, in the order of the jobs. A job that doesn't run
    // gets infinity. The seconds of one pass can be the costs of a later pass over the same jobs.
    std::vector<double>* seconds = nullptr;
};

// Length analysis of each job, spread over `threads` threads. `analyze(job)` analyzes one job, sets its `timing` and
// returns true, or returns false for a job that isn't analyzed. A job has a `label` and an optional `timing`. `analyze`
// reports a failed analysis by returning true with `timing` left empty. AnalyzeLengths then prints a warning for the
// job. `analyze` mustn't throw anything but std::bad_alloc. An analysis that runs out of memory stops them all and
// throws. The costs in `settings` set the order in which the jobs start.
template <typename Job, typename Analyze>
void AnalyzeLengths(std::vector<Job>& jobs, unsigned threads, const Analyze& analyze,
                    const AnalysisSettings& settings = {})
{
    threads = std::max(1u, std::min<unsigned>(threads, static_cast<unsigned>(jobs.size())));

    // The indices of the jobs, in the order in which they start.
    const std::vector<std::size_t> order = detail::StartOrder(jobs.size(), threads, settings.costs);

    if (settings.seconds)
    {
        settings.seconds->assign(jobs.size(), std::numeric_limits<double>::infinity());
    }

    std::atomic<std::size_t> done{0};
    const auto analyze_job = [&](std::size_t k)
    {
        const std::size_t i = order[k];
        const auto start = std::chrono::steady_clock::now();
        const bool analyzed = analyze(jobs[i]);
        if (settings.seconds)
        {
            (*settings.seconds)[i] = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
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
    };

    // When an analysis runs out of memory, no other job starts.
    try
    {
        RunParallel(jobs.size(), threads, analyze_job);
    }
    catch (const std::bad_alloc&)
    {
        throw std::runtime_error(settings.out_of_memory);
    }
}

} // namespace xsflib::cli
