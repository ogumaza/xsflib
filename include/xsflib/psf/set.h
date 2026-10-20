// SPDX-License-Identifier: MIT

// A set of PSF files: a file with its _lib/_libN chain. The loader is a template on the format's traits. A format
// copies the set's files into its programs, or holds each of them once (see streaming.h). The traits provide:
//
//   using ProcessDescriptor   game mode's descriptor: static Parse(reserved) -> std::optional<ProcessDescriptor>, and
//                             Check() const -> std::optional<std::string>, the reason a version 1 player rejects it;
//                             or void for a format without game mode
//   using ArchiveDescriptor   archive mode's descriptor, the same way, or void for a format without archive mode
//   kPsfVersion               the format's PSF version byte
//   kName                     the format's name ("zsf"), in "not a zsf file"; or kFile, the whole phrase with its
//                             article, for a name that takes "an" ("an xyz file")
//   kDriverBlock              where game mode's driver keeps its parameter block (DriverBlockLayout), for a format
//                             that copies its files
//   kStreaming                true for a format that holds each file once. The loader then reads a file's program,
//                             straight into its chunks, after the file's _lib. Such a format has no MEM chunks and no
//                             driver block. Every set of the format selects a sound. Without kStreaming, a format
//                             copies.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "xsflib/psf/program.h"
#include "xsflib/psf/psf.h"
#include "xsflib/psf/streaming.h"

namespace xsflib::psf
{

// Whether a format has game mode, and whether it has archive mode.
template <typename Format>
constexpr bool kHasGameMode = !std::is_void_v<typename Format::ProcessDescriptor>;
template <typename Format>
constexpr bool kHasArchiveMode = !std::is_void_v<typename Format::ArchiveDescriptor>;

// Whether a format holds each of a set's files once (see streaming.h).
template <typename Format>
constexpr bool kStreaming = requires { requires Format::kStreaming; };

namespace detail
{

// The descriptor of a mode that a format doesn't have. A set of such a format never holds one.
struct NoProcess
{
};

struct NoArchive
{
};

// "a zsf file", or the format's whole phrase (kFile).
template <typename Format>
std::string AFileOf()
{
    if constexpr (requires { Format::kFile; })
    {
        return Format::kFile;
    }
    else
    {
        return "a " + std::string(Format::kName) + " file";
    }
}

} // namespace detail

template <typename Format>
using ProcessDescriptorOf =
    std::conditional_t<kHasGameMode<Format>, typename Format::ProcessDescriptor, detail::NoProcess>;

template <typename Format>
using ArchiveDescriptorOf =
    std::conditional_t<kHasArchiveMode<Format>, typename Format::ArchiveDescriptor, detail::NoArchive>;

// A complete set: the chunks of a file and its libraries in loading order, the effective descriptor (exactly one of
// `descriptor` and `archive`) and the tags of the top-level file.
template <typename Format>
struct LoadedSet
{
    // The data of the file at `path` among the set's FILE chunks, or nullptr if there's none (see FindFile).
    const std::vector<uint8_t>* File(const std::string& path) const
    {
        return FindFile(files, path);
    }

    std::vector<uint8_t>* File(const std::string& path)
    {
        return const_cast<std::vector<uint8_t>*>(FindFile(files, path));
    }

    std::vector<MemChunk> memory;
    std::vector<FileChunk> files;
    std::optional<ProcessDescriptorOf<Format>> descriptor; // game mode
    std::optional<ArchiveDescriptorOf<Format>> archive;    // archive mode

    // Archive mode: the sounds that the last SND chunk in loading order selects, in its order, and the first of them.
    // A format that plays one sound takes `sound`.
    std::optional<uint32_t> sound;
    std::vector<uint32_t> sounds;

    Tags tags;
    std::vector<std::string> sources; // files loaded, in order (for diagnostics)
};

namespace detail
{

// Reads the chunks of the file at `path` into `chunks`. `data` is the file, and `psf` is its container. A streaming
// format reads them straight from the compressed program. A copying format splits the decompressed program.
template <typename Format>
std::optional<std::string> ReadChunks(const std::string& path, const std::vector<uint8_t>& data, const PsfFile& psf,
                                      ProgramChunks& chunks)
{
    if constexpr (kStreaming<Format>)
    {
        if (auto err = ParsePsfChunks(data, chunks))
        {
            return path + ": " + *err;
        }
    }
    else
    {
        // A format without archive mode skips SND chunks.
        if (auto err = ParseProgram(psf.program, chunks, ProgramOptions{.sound_chunks = kHasArchiveMode<Format>}))
        {
            return path + ": " + *err;
        }
    }

    return std::nullopt;
}

// Adds a file's chunks to the set, after those of the files loaded before it. A file that selects sounds replaces the
// set's sounds, the whole list.
template <typename Format>
void AddChunks(ProgramChunks& chunks, LoadedSet<Format>& out)
{
    for (auto& m : chunks.memory)
    {
        out.memory.push_back(std::move(m));
    }

    for (auto& f : chunks.files)
    {
        out.files.push_back(std::move(f));
    }

    if (chunks.sound)
    {
        out.sound = chunks.sound;
        out.sounds = std::move(chunks.sounds);
    }
}

// Takes the descriptor in `reserved` as the set's mode. `reserved` is the reserved area of the file at `path`. The last
// descriptor in loading order sets the mode. A file that holds neither descriptor leaves the mode as it is. Returns an
// error when the descriptor fails its check.
template <typename Format>
std::optional<std::string> TakeDescriptor(const std::string& path, const std::vector<uint8_t>& reserved,
                                          LoadedSet<Format>& out)
{
    bool game = false;
    if constexpr (kHasGameMode<Format>)
    {
        if (auto d = Format::ProcessDescriptor::Parse(reserved))
        {
            if (auto err = d->Check())
            {
                return path + ": " + *err;
            }

            out.descriptor = d;
            out.archive.reset();
            game = true;
        }
    }

    if constexpr (kHasArchiveMode<Format>)
    {
        if (auto a = game ? std::nullopt : Format::ArchiveDescriptor::Parse(reserved))
        {
            if (auto err = a->Check())
            {
                return path + ": " + *err;
            }

            out.archive = a;
            out.descriptor.reset();
        }
    }

    return std::nullopt;
}

// Loads the file at `path` into `out`: first its _lib, then the file itself, then its _lib2 and on. `depth` counts the
// library tags that led to the file. The file at depth 0 heads the set and gives it its tags.
template <typename Format>
std::optional<std::string> LoadRecursive(const std::string& path, const FileReader& reader, std::string_view separators,
                                         LoadedSet<Format>& out, int depth)
{
    if (depth > kMaxLibDepth)
    {
        return "_lib nesting too deep";
    }

    if (out.sources.size() >= kMaxSetFiles)
    {
        return "more than " + std::to_string(kMaxSetFiles) + " files in the set";
    }

    std::vector<uint8_t> data;
    if (!reader(path, data))
    {
        return "can't read " + path;
    }

    // A streaming format reads the program later, straight into its chunks.
    PsfFile psf;
    if (auto err = ParsePsf(data, psf, !kStreaming<Format>))
    {
        return path + ": " + *err;
    }

    if (psf.version != Format::kPsfVersion)
    {
        return path + ": not " + AFileOf<Format>() + " (PSF version " + Hex(psf.version, 2) + ")";
    }

    const std::string dir = DirectoryOf(path, separators);
    auto lib = psf.tags.find("_lib");
    if (lib != psf.tags.end() && !lib->second.empty())
    {
        if (auto err = LoadRecursive(dir + lib->second, reader, separators, out, depth + 1))
        {
            return err;
        }
    }

    ProgramChunks chunks;
    if (auto err = ReadChunks<Format>(path, data, psf, chunks))
    {
        return err;
    }

    AddChunks(chunks, out);

    if (auto err = TakeDescriptor(path, psf.reserved, out))
    {
        return err;
    }

    out.sources.push_back(path);

    for (int n = 2;; n++)
    {
        auto it = psf.tags.find("_lib" + std::to_string(n));
        if (it == psf.tags.end())
        {
            break;
        }

        // An empty one names no library. So does an empty _lib.
        if (it->second.empty())
        {
            continue;
        }

        if (auto err = LoadRecursive(dir + it->second, reader, separators, out, depth + 1))
        {
            return err;
        }
    }

    if (depth == 0)
    {
        out.tags = psf.tags;
    }

    return std::nullopt;
}

} // namespace detail

// Loads a file of the format with its _lib/_libN chain. Paths in _lib tags are relative to the directory of the file
// that names them: its path up to the last of `separators`. Returns an error for a set a version 1 player can't play:
// one without a descriptor, an archive-mode set without a sound (any set of a streaming format), a descriptor that
// fails its Check(), or a game-mode set whose driver block a version 1 player rejects (see CheckDriverBlock).
template <typename Format>
std::optional<std::string> LoadSet(const std::string& path, const FileReader& reader, LoadedSet<Format>& out,
                                   std::string_view separators = "/\\")
{
    out = LoadedSet<Format>{};

    if (auto err = detail::LoadRecursive(path, reader, separators, out, 0))
    {
        return err;
    }

    if constexpr (!kHasGameMode<Format>)
    {
        if (!out.archive)
        {
            return "no archive descriptor in " + path + " or its libraries";
        }
    }
    else if constexpr (kHasArchiveMode<Format>)
    {
        if (!out.descriptor && !out.archive)
        {
            return "no process or archive descriptor in " + path + " or its libraries";
        }
    }
    else if (!out.descriptor)
    {
        return "no process descriptor in " + path + " or its libraries";
    }

    if ((kStreaming<Format> || out.archive) && !out.sound)
    {
        return "no sound selected (SND chunk) in " + path + " or its libraries";
    }

    if constexpr (!kStreaming<Format>)
    {
        if (out.descriptor)
        {
            if (auto err = CheckDriverBlock(out.memory, Format::kDriverBlock))
            {
                return path + ": " + *err;
            }
        }
    }

    return std::nullopt;
}

} // namespace xsflib::psf
