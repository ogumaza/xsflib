// SPDX-License-Identifier: MIT

// A descriptor of a test format, for the tests of PSF sets (xsflib/psf/set.h).

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "xsflib/bytes.h"
#include "xsflib/psf/psf.h"

// A descriptor that a reserved area holds when it starts with `magic`. Its second word is its version. Both words are
// little-endian. A version 1 player accepts version 1 only.
template <uint32_t magic>
struct TestDescriptor
{
    static std::optional<TestDescriptor> Parse(const std::vector<uint8_t>& data)
    {
        if (data.size() < 8 || xsflib::LoadLe32(data.data()) != magic)
        {
            return std::nullopt;
        }

        return TestDescriptor{xsflib::LoadLe32(data.data() + 4)};
    }

    std::optional<std::string> Check() const
    {
        if (version != 1)
        {
            return "descriptor version " + std::to_string(version);
        }

        return std::nullopt;
    }

    // The reserved area that contains the descriptor.
    std::vector<uint8_t> Serialize() const
    {
        std::vector<uint8_t> v;
        xsflib::psf::Put32(v, magic);
        xsflib::psf::Put32(v, version);

        return v;
    }

    uint32_t version = 1;
};
