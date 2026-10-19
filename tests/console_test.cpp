// SPDX-License-Identifier: MIT

// Checks the console helpers (xsflib/console.h): the arguments come back one for one. On Windows they're read from the
// wide command line. Elsewhere they're argv itself. The other helpers do nothing unless the tool has a console window
// to itself.

#include "xsflib/console.h"

#include <cstdio>
#include <string>
#include <vector>

#include "xsflib/testing/check.h"

int main(int argc, char** argv)
{
    const std::vector<std::string> args = xsflib::console::Arguments(argc, argv);
    XSFLIB_CHECK(static_cast<int>(args.size()) == argc);
#ifndef _WIN32
    XSFLIB_CHECK(args == std::vector<std::string>(argv, argv + argc));
#endif

    // ctest runs the test in a console that it shares, or in none. The tool then neither waits nor prints.
    xsflib::console::LineBufferOutput();
    xsflib::console::UseUtf8Output(true);
    xsflib::console::WaitInOwnWindow(stderr, "\nPress Enter to close this window.");

    return xsflib::testing::Summary("console");
}
