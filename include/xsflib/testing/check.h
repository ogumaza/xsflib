// SPDX-License-Identifier: MIT

// The checks that the unit tests share. They're seqlib's checks (seqlib/testing/check.h) under xsflib's names. A failed
// check is reported and counted, and the test goes on. One run then lists every failure. RunTest runs the checks of a
// test's main and gives its exit status.

#pragma once

#include <exception>
#include <ostream>

#include "seqlib/testing/check.h"

namespace xsflib::testing
{

using seqlib::testing::Check;
using seqlib::testing::ErrorOf;
using seqlib::testing::failures;
using seqlib::testing::Summary;

// Runs `body`, the checks of a test's main. Returns the exit status that Summary(name) gives: 0 when every check passed
// and 1 after a failure. An exception that escapes `body` counts as a failure. The exception's message goes to the
// failure stream before the summary. A body that can't go on fails a check and returns.
template <typename Body>
int RunTest(const char* name, Body body)
{
    try
    {
        body();
    }
    catch (const std::exception& e)
    {
        failures++;
        *seqlib::testing::failure_stream << "exception: " << e.what() << '\n' << std::flush;
    }

    return Summary(name);
}

} // namespace xsflib::testing

// seqlib's SEQLIB_CHECK, under xsflib's name.
#define XSFLIB_CHECK(cond) SEQLIB_CHECK(cond)
