// SPDX-License-Identifier: MIT

// Checks the SHA-256 of a render's samples (xsflib/testing/samples_sha256.h): the bytes that it hashes, their order,
// and the frames that it picks.

#include "xsflib/testing/samples_sha256.h"

#include <cstdint>
#include <vector>

#include "xsflib/testing/check.h"

using xsflib::testing::SamplesSha256;

int main()
{
    // Two stereo frames. Their samples' bytes, little-endian, are 34 12 00 80 and 03 00 fc ff.
    const std::vector<int16_t> kSamples = {0x1234, -32768, 3, -4};

    XSFLIB_CHECK(SamplesSha256(kSamples, 0, 2) == "4bf03cf5bb37fa6e954d0c74d351d08b2cf30878de7ad0e2c0fb6b71a3371b57");
    XSFLIB_CHECK(SamplesSha256(kSamples, 0, 1) == "2d23ffbe3ea80b925f22844c0f09a236b98d12b91c8e1797a5047218babc8c79");
    XSFLIB_CHECK(SamplesSha256(kSamples, 1, 1) == "b5014a32d2446dd3e65febdd1eeb4f20d7edb545387d4b89034bfaf61c3ef14a");
    XSFLIB_CHECK(SamplesSha256(kSamples, 2, 0) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

    return xsflib::testing::Summary("samples sha256");
}
