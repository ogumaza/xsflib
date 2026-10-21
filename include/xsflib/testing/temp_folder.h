// SPDX-License-Identifier: MIT

// A folder for a test's files in the system's temporary folder. Each run of a test gets a new folder. Two runs of one
// test at the same time, such as a Release and a Debug suite side by side, then keep their files apart.

#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>

namespace xsflib::testing
{

// Makes a new, empty folder in the system's temporary folder, and deletes it with its files when it goes out of scope.
// The folder's name is `name` and a time stamp.
class TempFolder
{
public:
    explicit TempFolder(const std::string& name)
        : path_(std::filesystem::temp_directory_path() /
                (name + "_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
    {
        std::filesystem::create_directories(path_);
    }

    TempFolder(const TempFolder&) = delete;
    TempFolder& operator=(const TempFolder&) = delete;

    ~TempFolder()
    {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    const std::filesystem::path& Path() const
    {
        return path_;
    }

private:
    std::filesystem::path path_;
};

} // namespace xsflib::testing
