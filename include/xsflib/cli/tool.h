// SPDX-License-Identifier: MIT

// The main around a command-line tool's run, and the check of the play and rip tools' inputs and the loop over them
// (see play.h and rip.h). A tool that uses RunTool links xsflib::console. The play and rip tools also share the reading
// of their command lines (ParseArguments) and the steps of their runs (RunInputs).

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
#include "xsflib/rip/tool.h"

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
//
// On Windows, output to a file or a pipe goes out unbuffered (see console::UnbufferRedirectedOutput). The window waits
// only while stdin and stdout are the console (see console::WaitInOwnWindow).
template <typename Run>
int RunTool(const Run& run, const ToolConsole& setup = {})
{
    xsflib::console::UseUtf8Output(setup.restore_code_page);
#ifdef _WIN32
    xsflib::console::UnbufferRedirectedOutput();
#endif
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
        const std::string& in = inputs[i];
        const fs::path path = rip::InputPath(in);

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

// The value of an option on a command line: the argument after the option.
class OptionValue
{
public:
    // The value of the option at argv[i]. Reading the value moves `i` past it.
    OptionValue(int argc, char** argv, int& i) : argc_(argc), argv_(argv), i_(i)
    {
    }

    // Reads the value. At the end of the command line, it gives an empty string, and Missing() becomes true.
    std::string operator()()
    {
        if (i_ + 1 >= argc_)
        {
            missing_ = true;
            return "";
        }

        return argv_[++i_];
    }

    // Whether the command line ended before the value.
    bool Missing() const
    {
        return missing_;
    }

private:
    int argc_;
    char** argv_;
    int& i_;
    bool missing_ = false;
};

// Reads the command line of a play or rip tool. -h and --help print the help to stdout with `usage(stdout)`. --version
// prints the tool's name and version. `option(a, value)` takes the tool's options. It returns -1 if `a` isn't one of
// them, 0 when it took `a`, or the exit code after it prints an error. An option whose value is missing gets the error
// "error: <option> <tool.requires_a_value>". Any other argument that starts with '-' is an unknown option, and the rest
// are the inputs. Without inputs, the help goes to stderr. Returns 0 to go on, -1 when there's nothing more to do
// (after --help or --version), or the exit code.
template <typename Tool, typename Usage, typename Option>
int ParseArguments(const Tool& tool, int argc, char** argv, const Usage& usage, const Option& option,
                   std::vector<std::string>& inputs)
{
    for (int i = 1; i < argc; i++)
    {
        const std::string a = argv[i];
        if (a == "-h" || a == "--help")
        {
            usage(stdout);
            return -1;
        }

        if (a == "--version")
        {
            std::printf("%s %s\n", tool.name.c_str(), tool.version.c_str());
            return -1;
        }

        OptionValue value(argc, argv, i);
        const int taken = option(a, value);
        if (taken > 0)
        {
            return taken;
        }

        if (taken < 0 && a.size() > 1 && a[0] == '-')
        {
            std::fprintf(stderr, "error: unknown option %s (see --help)\n", a.c_str());
            return 2;
        }

        if (taken < 0)
        {
            inputs.push_back(a);
        }

        if (value.Missing())
        {
            std::fprintf(stderr, "error: %s %s\n", a.c_str(), tool.requires_a_value.c_str());
            return 2;
        }
    }

    if (inputs.empty())
    {
        usage(stderr);
        return 2;
    }

    return 0;
}

// The steps of a play or rip tool's run. `parse(inputs)` reads the command line, and returns what ParseArguments does.
// Then every input must exist (InputsExist, with `output`). `before(inputs)` runs once before the first input, and
// returns 0 to go on, or the exit code. Then `run(input)` runs on each input (RunEachInput). Returns the exit code.
template <typename Parse, typename Before, typename Run>
int RunInputs(const Parse& parse, const char* output, const Before& before, const Run& run)
{
    std::vector<std::string> inputs;
    if (const int parsed = parse(inputs))
    {
        return parsed < 0 ? 0 : parsed;
    }

    if (!InputsExist(inputs, output))
    {
        return 1;
    }

    if (const int result = before(inputs))
    {
        return result;
    }

    return RunEachInput(inputs, run);
}

} // namespace detail
} // namespace xsflib::cli
