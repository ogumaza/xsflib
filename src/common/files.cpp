// SPDX-License-Identifier: MIT

#include "xsflib/files.h"

#include <fstream>
#include <ios>
#include <iterator>

namespace xsflib
{

namespace fs = std::filesystem;

fs::path PathFromUtf8(const std::string& s)
{
    return fs::path(std::u8string(s.begin(), s.end()));
}

std::string Utf8(const fs::path& p)
{
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}

bool ReadStream(std::istream& in, std::vector<uint8_t>& data, std::size_t max_size)
{
    // Read the whole file at once. Copying it a byte at a time took 170 ms for an 18 MB library.
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    in.seekg(0);
    if (size < 0 || !in)
    {
        // Read streams with unknown sizes, such as pipes, until EOF.
        in.clear();
        data.assign(std::istreambuf_iterator<char>(in), {});
        return data.size() <= max_size;
    }

    if (static_cast<uint64_t>(size) > max_size)
    {
        return false;
    }

    data.resize(static_cast<std::size_t>(size));
    in.read(reinterpret_cast<char*>(data.data()), size);
    data.resize(static_cast<std::size_t>(in.gcount()));

    return true;
}

bool ReadFile(const fs::path& path, std::vector<uint8_t>& data, std::size_t max_size)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
    {
        return false;
    }

    return ReadStream(f, data, max_size);
}

std::optional<std::string> WriteFile(const fs::path& path, std::span<const uint8_t> data)
{
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    f.close();
    if (!f)
    {
        return "can't write " + Utf8(path);
    }

    return std::nullopt;
}

} // namespace xsflib
