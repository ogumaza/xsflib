// SPDX-License-Identifier: MIT

// Checks the helpers of the oracle tests (xsflib/testing/oracle_tally.h): the text of a value, the tally of the cases
// in which a game and a model differ, and the generator of inputs. A tally passes when every case matches, and fails
// once at its end otherwise. The failing tallies report a few cases to stderr.

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "xsflib/testing/check.h"
#include "xsflib/testing/oracle_tally.h"

using seqlib::testing::Capture;
using seqlib::testing::Captured;
using xsflib::testing::OracleInputs;
using xsflib::testing::OracleText;
using xsflib::testing::Tally;

namespace
{

void TestText()
{
    XSFLIB_CHECK(OracleText(1.5f) == "0x1.8p+0" && OracleText(-0.0f) == "-0x0p+0");
    XSFLIB_CHECK(OracleText(uint64_t{42}) == "42" && OracleText(-7) == "-7");
}

// A float matches only when its bits do: -0 isn't 0, and a NaN matches the same NaN.
void TestFloats()
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    std::optional<int> cases;
    std::optional<int> differ;
    const Captured same = Capture(
        [&]
        {
            Tally tally("same floats");
            tally.Compare(1.0f, 1.0f, "1");
            tally.Compare(nan, nan, "nan");
            cases = tally.Cases();
            differ = tally.Differ();
        });
    XSFLIB_CHECK(same.failures == 0 && cases == 2 && differ == 0);

    const Captured zero = Capture(
        [&]
        {
            Tally tally("signed zeros");
            tally.Compare(0.0f, -0.0f, "0");
            tally.Compare(2.0f, std::nextafter(2.0f, 3.0f), "2");
            tally.Compare(3.0f, 3.0f, "3");
            cases = tally.Cases();
            differ = tally.Differ();
        });
    XSFLIB_CHECK(zero.failures == 1 && cases == 3 && differ == 2);
    XSFLIB_CHECK(zero.text.find("signed zeros matches the game in all 3 cases (2 differ)") != std::string::npos);
}

// Whole numbers, and lists of values. A list matches when its size and the bits of every value match.
void TestIntegersAndLists()
{
    std::optional<int> differ;
    const Captured integers = Capture(
        [&]
        {
            Tally tally("integers");
            tally.Compare(uint64_t{5}, uint64_t{5}, "5");
            tally.Compare(uint64_t{0xffffffffffffffff}, uint64_t{0xfffffffffffffffe}, "max");
            differ = tally.Differ();
        });
    XSFLIB_CHECK(integers.failures == 1 && differ == 1);

    const Captured lists = Capture(
        [&]
        {
            Tally tally("lists");
            tally.Compare(std::vector<float>{1.0f, 2.0f}, std::vector<float>{1.0f, 2.0f}, "same");
            tally.Compare(std::vector<float>{1.0f, 2.0f}, std::vector<float>{1.0f, -2.0f}, "one value");
            tally.Compare(std::vector<int32_t>{1, 2}, std::vector<int32_t>{1}, "shorter");
            tally.Compare(std::vector<int32_t>{}, std::vector<int32_t>{}, "empty");
            differ = tally.Differ();
        });
    XSFLIB_CHECK(lists.failures == 1 && differ == 2);
}

// The generator gives the same numbers on every run, each below its bound.
void TestInputs()
{
    OracleInputs inputs;
    OracleInputs again;
    bool same = true;
    bool below = true;
    for (uint32_t n = 1; n <= 1000; ++n)
    {
        const uint32_t x = inputs.Below(n);
        same = same && again.Below(n) == x;
        below = below && x < n;
    }
    XSFLIB_CHECK(same && below);

    // The first numbers below 2^31 are fixed. The inputs of the oracle tests then stay the same.
    OracleInputs fresh;
    const uint32_t first = fresh.Below(0x80000000u);
    const uint32_t second = fresh.Below(0x80000000u);
    const uint32_t third = fresh.Below(0x80000000u);
    XSFLIB_CHECK(first == 0x16757710u && second == 0x55403aa6u && third == 0x59e24825u);
}

} // namespace

int main()
{
    const auto tests = []
    {
        TestText();
        TestFloats();
        TestIntegersAndLists();
        TestInputs();
    };

    return xsflib::testing::RunTest("testing oracle tally", tests);
}
