// SPDX-License-Identifier: MIT

// Checks the bytes of a render's samples and their SHA-256 (xsflib/testing/samples_sha256.h): the bytes, their order,
// and the frames that the SHA-256 picks.

#include "xsflib/testing/samples_sha256.h"

#include <cstdint>
#include <vector>

#include "xsflib/testing/check.h"

using xsflib::testing::SampleBytes;
using xsflib::testing::SamplesSha256;

int main()
{
    const auto tests = []
    {
        // Two stereo frames. Their samples' bytes, little-endian, are 34 12 00 80 and 03 00 fc ff.
        const std::vector<int16_t> kSamples = {0x1234, -32768, 3, -4};

        XSFLIB_CHECK(SampleBytes(kSamples) == std::vector<uint8_t>({0x34, 0x12, 0x00, 0x80, 0x03, 0x00, 0xfc, 0xff}));
        XSFLIB_CHECK(SampleBytes(std::vector<int16_t>()).empty());
        XSFLIB_CHECK(SamplesSha256(kSamples, 0, 2) ==
                     "4bf03cf5bb37fa6e954d0c74d351d08b2cf30878de7ad0e2c0fb6b71a3371b57");
        XSFLIB_CHECK(SamplesSha256(kSamples, 0, 1) ==
                     "2d23ffbe3ea80b925f22844c0f09a236b98d12b91c8e1797a5047218babc8c79");
        XSFLIB_CHECK(SamplesSha256(kSamples, 1, 1) ==
                     "b5014a32d2446dd3e65febdd1eeb4f20d7edb545387d4b89034bfaf61c3ef14a");
        XSFLIB_CHECK(SamplesSha256(kSamples, 2, 0) ==
                     "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    };

    return xsflib::testing::RunTest("samples sha256", tests);
}
