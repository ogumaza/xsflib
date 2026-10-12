// SPDX-License-Identifier: MIT

// The console of a command-line tool: its arguments in UTF-8, UTF-8 or line-buffered output, and a window that waits
// before it closes. Most of them do something only on Windows. Elsewhere the arguments come as they are. Only the line
// buffering does anything there.

#pragma once

#include <cstdio>
#include <string>
#include <vector>

namespace xsflib::console
{

// The program's arguments in UTF-8. On Windows they're read as wide strings. Paths outside the ANSI code page then
// survive. Elsewhere, or if Windows can't give them, they're argv as it is.
std::vector<std::string> Arguments(int argc, char** argv);

// Line-buffers stdout to keep it in order with the errors on stderr when both go into one pipe. A macOS droplet app
// (xsflib_add_droplet) reads such a pipe. Windows' C runtime has no line buffering. There, stdout stays as it is.
void LineBufferOutput();

// Sets the console's output code page to UTF-8. With `restore`, the old code page comes back when the program ends.
// That includes an end by Ctrl+C or by the window's close button.
void UseUtf8Output(bool restore);

// When the program has a console window of its own, writes `prompt` to `stream` and waits for Enter. Explorer gives a
// program a window of its own when files are dropped on it, or when it's double-clicked. That window closes when the
// program ends, before its messages can be read.
void WaitInOwnWindow(std::FILE* stream, const char* prompt);

} // namespace xsflib::console
