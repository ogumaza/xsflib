// SPDX-License-Identifier: MIT

#include "xsflib/console.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
// shellapi.h, for CommandLineToArgvW, requires windows.h first.
#include <shellapi.h>
#endif

#include <cstdlib>
#include <utility>

namespace xsflib::console
{
namespace
{

#ifdef _WIN32
UINT original_output_cp = 0; // the console's code page before UseUtf8Output, or 0 if it wasn't changed

void RestoreConsole()
{
    if (original_output_cp != 0)
    {
        SetConsoleOutputCP(original_output_cp);
    }
}

// Restores the code page when Ctrl+C or closing the window ends the tool. Ending the tool this way skips the atexit
// functions. The default handler then ends the process.
BOOL WINAPI OnConsoleControl(DWORD)
{
    RestoreConsole();

    return FALSE;
}
#endif

} // namespace

std::vector<std::string> Arguments(int argc, char** argv)
{
    std::vector<std::string> args(argv, argv + argc);
#ifdef _WIN32
    int wargc = 0;
    if (LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &wargc))
    {
        args.clear();
        for (int i = 0; i < wargc; i++)
        {
            const int n = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, nullptr, 0, nullptr, nullptr);
            std::string a(n > 0 ? n - 1 : 0, '\0');
            if (n > 1)
            {
                WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, a.data(), n, nullptr, nullptr);
            }

            args.push_back(std::move(a));
        }

        LocalFree(wargv);
    }
#endif

    return args;
}

void LineBufferOutput()
{
#ifndef _WIN32
    std::setvbuf(stdout, nullptr, _IOLBF, BUFSIZ);
#endif
}

void UseUtf8Output(bool restore)
{
#ifdef _WIN32
    if (!restore)
    {
        SetConsoleOutputCP(CP_UTF8);
        return;
    }

    const UINT cp = GetConsoleOutputCP();
    if (cp != 0 && cp != CP_UTF8 && SetConsoleOutputCP(CP_UTF8))
    {
        original_output_cp = cp;
        std::atexit(RestoreConsole);
        SetConsoleCtrlHandler(OnConsoleControl, TRUE);
    }
#else
    (void)restore;
#endif
}

void WaitInOwnWindow(std::FILE* stream, const char* prompt)
{
#ifdef _WIN32
    // A console that Explorer created for the tool has only the tool's process attached.
    DWORD processes[2];
    if (GetConsoleProcessList(processes, 2) == 1)
    {
        std::fputs(prompt, stream);
        std::fflush(stream);
        std::getchar();
    }
#else
    (void)stream;
    (void)prompt;
#endif
}

} // namespace xsflib::console
