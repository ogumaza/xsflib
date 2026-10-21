// SPDX-License-Identifier: MIT

// The main around a command-line tool's run, and the check of the play and rip tools' inputs and the loop over them
// (see play.h and rip.h). A tool that uses RunTool links xsflib::console.

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <set>
#include <string>
#include <system_error>
#include <vector>

#include "xsflib/console.h"

namespace xsflib::cli
{

// How a tool's main treats the console. The defaults are the play and rip tools' settings.
struct ToolConsole
{
    // Whether the console's code page comes back when the tool exits (see console::UseUtf8Output).
    bool restore_code_page = true;

    // Whether stdout is line-buffered (see console::LineBufferOutput).
    bool line_buffered = false;

    // Where the prompt goes when Windows opened a console window for the tool alone, and the prompt's text.
    std::FILE* prompt_stream = stderr;
    const char* prompt = "\nPress Enter to close this window.";
};

// A tool's main: sets the console up from `setup`, calls `run`, and keeps open a console window that Windows opened for
// the tool alone. Returns `run`'s exit code. An error that escapes `run`, an allocation failure during a long run among
// them, prints its message and gives 1.
template <typename Run>
int RunTool(const Run& run, const ToolConsole& setup = {})
{
    xsflib::console::UseUtf8Output(setup.restore_code_page);
    if (setup.line_buffered)
    {
        xsflib::console::LineBufferOutput();
    }

    int result = 1;
    try
    {
        result = run();
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "error: %s\n", e.what());
    }

    xsflib::console::WaitInOwnWindow(setup.prompt_stream, setup.prompt);

    return result;
}

namespace detail
{

// Checks that every input exists, before anything is written. Dropped files always exist. The check catches typos, and
// output paths passed as positional arguments instead of with -o. `output` is what -o names: "file" or "folder".
// Returns false after printing the error.
inline bool InputsExist(const std::vector<std::string>& inputs, const char* output)
{
    const std::string hint = std::string(" (-o names the output ") + output + ")";
    for (std::size_t i = 0; i < inputs.size(); i++)
    {
        std::error_code ec;
        if (!std::filesystem::exists(inputs[i], ec))
        {
            std::fprintf(stderr, "error: %s: no such file or folder%s\n", inputs[i].c_str(), i ? hint.c_str() : "");
            return false;
        }
    }

    return true;
}

// Runs `run(input)` on each input in turn, and returns the highest exit code. A failed input doesn't stop the rest.
// With more than one input, a line with the input's name and a colon comes before its messages. The macOS droplet
// reports each input from the messages after its line. An input whose canonical path is an earlier input's is skipped.
// Where the canonical path can't be found, the absolute path without a trailing separator takes its place. An exception
// from `run` is reported as the input's error.
template <typename Run>
int RunEachInput(const std::vector<std::string>& inputs, const Run& run)
{
    namespace fs = std::filesystem;

    std::set<fs::path> done;
    int result = 0;
    for (std::size_t i = 0; i < inputs.size(); i++)
    {
        // The input's absolute path, without the separator at the end of a folder given with one. It gives the input's
        // name.
        const std::string& in = inputs[i];
        fs::path path = fs::absolute(in).lexically_normal();
        if (!path.has_filename())
        {
            path = path.parent_path();
        }

        if (inputs.size() > 1)
        {
            std::fprintf(stderr, "%s%s:\n", i ? "\n" : "", path.filename().string().c_str());
        }

        std::error_code ec;
        const fs::path canonical = fs::weakly_canonical(in, ec);
        if (!done.insert(ec ? path : canonical).second)
        {
            std::fprintf(stderr, "skipped duplicate input\n");
            continue;
        }

        try
        {
            result = std::max(result, run(in));
        }
        catch (const std::exception& e)
        {
            std::fprintf(stderr, "error: %s: %s\n", in.c_str(), e.what());
            result = std::max(result, 1);
        }
    }

    return result;
}

} // namespace detail
} // namespace xsflib::cli
