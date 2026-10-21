// SPDX-License-Identifier: MIT

// Checks RunTest (xsflib/testing/check.h): the exit status and the output of a test whose checks pass, fail or throw.

#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "xsflib/testing/check.h"

using xsflib::testing::failures;

namespace
{

// The exit status that RunTest gives for `body`, as the test "inner". `output` gets what RunTest writes. The failures
// in `body` don't count among the failures of this test. RunTest's output doesn't reach the output of this test.
template <typename Body>
int StatusOf(Body body, std::string& output)
{
    std::ostringstream caught;
    std::ostream* const failure_stream = seqlib::testing::failure_stream;
    std::ostream* const success_stream = seqlib::testing::success_stream;
    seqlib::testing::failure_stream = &caught;
    seqlib::testing::success_stream = &caught;
    const int before = failures;
    failures = 0;

    const int status = xsflib::testing::RunTest("inner", body);

    failures = before;
    seqlib::testing::failure_stream = failure_stream;
    seqlib::testing::success_stream = success_stream;
    output = caught.str();

    return status;
}

void TestRunTest()
{
    std::string output;

    XSFLIB_CHECK(StatusOf([] { XSFLIB_CHECK(1 + 1 == 2); }, output) == 0);
    XSFLIB_CHECK(output == "inner tests passed\n");

    // A failed check fails the test. The check's message comes before the count.
    XSFLIB_CHECK(StatusOf([] { XSFLIB_CHECK(1 + 1 == 3); }, output) == 1);
    XSFLIB_CHECK(output.find("CHECK failed: 1 + 1 == 3\n1 check(s) failed\n") != std::string::npos);

    // An exception fails the test too, after the checks before it.
    const auto throws = []
    {
        XSFLIB_CHECK(1 + 1 == 3);
        throw std::runtime_error("bad input");
    };

    XSFLIB_CHECK(StatusOf(throws, output) == 1);
    XSFLIB_CHECK(output.find("CHECK failed: 1 + 1 == 3\nexception: bad input\n2 check(s) failed\n") !=
                 std::string::npos);
}

} // namespace

int main()
{
    TestRunTest();

    return xsflib::testing::Summary("testing check");
}
