// SPDX-License-Identifier: MIT

// Checks RunTest (xsflib/testing/check.h): the exit status and the output of a test whose checks pass, fail or throw.

#include <stdexcept>
#include <string>

#include "xsflib/testing/check.h"

using seqlib::testing::Capture;

namespace
{

// The exit status that RunTest gives for `body`, as the test "inner". `output` gets what RunTest writes. The failures
// in `body` don't count among the failures of this test. RunTest's output doesn't reach the output of this test.
template <typename Body>
int StatusOf(Body body, std::string& output)
{
    int status = 0;
    output = Capture([&] { status = xsflib::testing::RunTest("inner", body); }).text;

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
    const auto tests = []
    {
        TestRunTest();
    };

    return xsflib::testing::RunTest("testing check", tests);
}
