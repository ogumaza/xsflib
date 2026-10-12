// SPDX-License-Identifier: MIT

#include "xsflib/psf/descriptor.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "xsflib/psf/program.h"
#include "xsflib/text.h"

namespace xsflib::psf
{

std::optional<std::string> CheckDescriptorVersion(std::string_view name, uint32_t version, uint32_t supported)
{
    if (version != supported)
    {
        return std::string(name) + " descriptor version " + std::to_string(version) +
               "; this player supports version " + std::to_string(supported);
    }

    return std::nullopt;
}

std::optional<std::string> CheckDescriptorFlags(std::string_view name, uint32_t flags, uint32_t required)
{
    if (flags != required)
    {
        const std::string expected = required == 0 ? "; version 1 has none" : FlagsExpected();
        return "the " + std::string(name) + " descriptor's flags are " + Hex(flags) + expected;
    }

    return std::nullopt;
}

std::optional<std::string> CheckDescriptorOutputMode(std::string_view name, uint32_t output_mode)
{
    if (output_mode != kDescriptorOutputStereo)
    {
        return "the " + std::string(name) + " descriptor's output mode is " + std::to_string(output_mode) +
               OutputModeExpected();
    }

    return std::nullopt;
}

std::optional<std::string> CheckDescriptorHeader(std::string_view name, uint32_t version, uint32_t flags,
                                                 uint32_t output_mode, uint32_t required_flags)
{
    if (auto err = CheckDescriptorVersion(name, version, 1))
    {
        return err;
    }

    if (auto err = CheckDescriptorFlags(name, flags, required_flags))
    {
        return err;
    }

    return CheckDescriptorOutputMode(name, output_mode);
}

} // namespace xsflib::psf
