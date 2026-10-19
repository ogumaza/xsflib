// SPDX-License-Identifier: MIT

// The end of the message about an output mode that version 1 doesn't play. The checks of the descriptors (see
// descriptor.h) and of the driver blocks (see program.h) share it.

#pragma once

namespace xsflib::psf::detail
{

inline constexpr char kOutputModeExpected[] = "; version 1 supports only stereo (1)";

} // namespace xsflib::psf::detail
