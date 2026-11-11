// SPDX-License-Identifier: MIT

// A command line for a test that calls a function that takes the arguments of a program's main, such as a tool's run.

#pragma once

#include <string>
#include <vector>

namespace xsflib::testing
{

// The arguments `argc` and `argv` of a program's main, made from strings. argv[0] is the program's name. argv[argc] is
// null. argv points into the command line. The command line has to outlive every use of argv.
class CommandLine
{
public:
    CommandLine(const std::string& program, const std::vector<std::string>& arguments)
    {
        arguments_.push_back(program);
        arguments_.insert(arguments_.end(), arguments.begin(), arguments.end());
        for (std::string& argument : arguments_)
        {
            argv_.push_back(argument.data());
        }

        argv_.push_back(nullptr);
    }

    CommandLine(const CommandLine&) = delete;
    CommandLine& operator=(const CommandLine&) = delete;

    int Argc() const
    {
        return static_cast<int>(arguments_.size());
    }

    char** Argv()
    {
        return argv_.data();
    }

private:
    std::vector<std::string> arguments_;
    std::vector<char*> argv_;
};

} // namespace xsflib::testing
