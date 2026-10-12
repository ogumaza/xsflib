// SPDX-License-Identifier: MIT

// Functions that the rip tools share (see tool.h).

#include "xsflib/rip/tool.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <ios>
#include <map>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "xsflib/ascii.h"
#include "xsflib/files.h"

namespace xsflib::rip
{

namespace fs = std::filesystem;

bool WriteFile(const fs::path& path, const std::vector<uint8_t>& data)
{
    return !xsflib::WriteFile(path, data);
}

fs::path InputPath(const std::string& input)
{
    const fs::path path = fs::absolute(input).lexically_normal();
    return path.has_filename() ? path : path.parent_path();
}

std::string NameOfInput(const std::string& input)
{
    const fs::path path = InputPath(input);
    return fs::is_directory(path) ? path.filename().string() : path.stem().string();
}

std::string ReadHead(const std::string& path, std::size_t size)
{
    std::string head;
    std::error_code ec;
    if (fs::is_regular_file(path, ec))
    {
        std::ifstream f(path, std::ios::binary);
        head.resize(size);
        f.read(head.data(), static_cast<std::streamsize>(head.size()));
        head.resize(static_cast<std::size_t>(f.gcount()));
    }

    return head;
}

fs::path DefaultOutputDir(const std::string& input, const std::string& suffix)
{
    return InputPath(input).parent_path() / (NameOfInput(input) + suffix);
}

void ReportOutput(const fs::path& out_dir)
{
    std::fprintf(stderr, "output: %s\n", fs::absolute(out_dir).lexically_normal().string().c_str());
}

std::vector<std::string> ArchiveFolders(const std::vector<std::string>& paths)
{
    // Each path's parts from the archive up: its name without the extension, then the folders above it.
    std::vector<std::vector<std::string>> parts;
    for (const auto& path : paths)
    {
        std::vector<std::string> p{fs::path(path).stem().string()};
        for (fs::path folder = fs::path(path).parent_path(); folder.has_filename(); folder = folder.parent_path())
        {
            p.push_back(folder.filename().string());
        }
        parts.push_back(std::move(p));
    }

    // Each round, the archives whose folders clash take one more part.
    std::vector<std::size_t> used(paths.size(), 1);
    std::vector<std::string> folders(paths.size());
    for (bool clash = true; clash;)
    {
        std::map<std::string, std::vector<std::size_t>> by_name;
        for (std::size_t i = 0; i < paths.size(); i++)
        {
            folders[i] = parts[i][0];
            for (std::size_t j = 1; j < used[i]; j++)
            {
                folders[i] = parts[i][j] + "_" + folders[i];
            }
            by_name[AsciiLower(folders[i])].push_back(i);
        }

        clash = false;
        for (const auto& [name, archives] : by_name)
        {
            for (const std::size_t i : archives)
            {
                if (archives.size() > 1 && used[i] < parts[i].size())
                {
                    used[i]++;
                    clash = true;
                }
            }
        }
    }

    return folders;
}

} // namespace xsflib::rip
