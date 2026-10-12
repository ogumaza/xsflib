// SPDX-License-Identifier: MIT

// The names of a rip's files.

#pragma once

#include <cstdint>
#include <set>
#include <string>

namespace xsflib::rip
{

// Filenames for the rips in one folder, valid on Windows, macOS and Linux. The names are UTF-8. A caller makes a path
// of one with PathFromUtf8. Names that differ only in the case of ASCII letters stay unique, even on case-insensitive
// filesystems. Each format gives the extensions of its library and its minis, such as ".xyzlib" and ".minixyz".
class FileNames
{
public:
    FileNames(std::string library_extension, std::string mini_extension);

    // "<base><library extension>".
    std::string Library(const std::string& base);

    // "<index, four digits> <label><mini extension>".
    std::string Mini(uint32_t index, const std::string& label);

private:
    std::string Unique(const std::string& stem, const std::string& extension);

    std::string library_extension_;
    std::string mini_extension_;
    std::set<std::string> taken_; // the names used so far, in lowercase
};

} // namespace xsflib::rip
