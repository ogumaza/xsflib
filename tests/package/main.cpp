// SPDX-License-Identifier: MIT

// Uses xsflib from another project: writes a PSF file in memory and reads it back.

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include "xsflib/psf/psf.h"
#include "xsflib/testing/check.h"

int main()
{
    const xsflib::psf::Tags kTags = {{"title", "Package test"}, {"length", "1:00"}};
    const std::vector<uint8_t> kReserved = {1, 2, 3, 4};
    const std::vector<uint8_t> kProgram(1000, 0x5a);

    const std::vector<uint8_t> file = xsflib::psf::WritePsf(0x27, kReserved, kProgram, kTags);
    xsflib::psf::PsfFile psf;
    const std::optional<std::string> error = xsflib::psf::ParsePsf(file, psf);

    XSFLIB_CHECK(!error);
    XSFLIB_CHECK(psf.version == 0x27 && psf.reserved == kReserved && psf.program == kProgram && psf.tags == kTags);
    XSFLIB_CHECK(xsflib::psf::ParseTime(psf.tags.at("length")) == 60000);

    if (xsflib::testing::failures)
    {
        std::fprintf(stderr, "%d check(s) failed\n", xsflib::testing::failures);
        return 1;
    }

    std::printf("package test passed\n");

    return 0;
}
