// SPDX-License-Identifier: MIT

// The checks that the unit tests share. They're seqlib's checks (seqlib/testing/check.h) under xsflib's names. A failed
// check is reported and counted, and the test goes on. One run then lists every failure.

#pragma once

#include "seqlib/testing/check.h"

namespace xsflib::testing
{

using seqlib::testing::Check;
using seqlib::testing::ErrorOf;
using seqlib::testing::failures;
using seqlib::testing::Summary;

} // namespace xsflib::testing

// seqlib's SEQLIB_CHECK, under xsflib's name.
#define XSFLIB_CHECK(cond) SEQLIB_CHECK(cond)
