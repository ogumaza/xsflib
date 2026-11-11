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
//
// The loader walks the set's files with WalkSet. A loader of another kind can walk a set with it too: WalkSet takes a
// callback that parses each file and one that adds it to the set.

#pragma once

#include <cstddef>
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

    // Archive mode: the sounds that the last SND chunk in loading order selects, in its order, and the first of them. A
    // format that plays one sound takes `sound`.
    std::optional<uint32_t> sound;
    std::vector<uint32_t> sounds;

    Tags tags;
    std::vector<std::string> sources; // files loaded, in order (for diagnostics)
};

// The limits of a walk over a set's files (WalkSet), and the errors of a set past them.
struct WalkRules
{
    // The error for a file more than kMaxLibDepth library tags down from the first file.
    std::string too_deep = "_lib nesting too deep";

    // The error for a set of more than kMaxSetFiles files.
    std::string too_many = "more than " + std::to_string(kMaxSetFiles) + " files in the set";

    // Whether a file counts toward kMaxSetFiles as soon as the walk reaches it. Otherwise a file counts once it's
    // added. A file whose _lib is still loading then doesn't count yet.
    bool count_reached = false;
};

namespace detail
{

// What a walk over a set's files uses at every level (see WalkSet).
template <typename Parse, typename Add>
struct SetWalk
{
    const FileReader& reader;
    std::string_view separators;
    const Parse& parse;
    const Add& add;
    const WalkRules& rules;
    std::size_t files = 0; // the files that count toward kMaxSetFiles so far
};

// Walks the file at `path`, `depth` library tags down from the first file, into `file` (see WalkSet).
template <typename File, typename Parse, typename Add>
std::optional<std::string> WalkFile(const std::string& path, int depth, File& file, SetWalk<Parse, Add>& walk)
{
    if (depth > kMaxLibDepth)
    {
        return walk.rules.too_deep;
    }

    if (walk.files >= kMaxSetFiles)
    {
        return walk.rules.too_many;
    }

    if (walk.rules.count_reached)
    {
        walk.files++;
    }

    std::vector<uint8_t> data;
    if (!walk.reader(path, data))
    {
        return "can't read " + path;
    }

    if (auto err = walk.parse(path, std::as_const(data), depth, file))
    {
        return err;
    }

    // The file's _lib loads before the file.
    const std::string dir = DirectoryOf(path, walk.separators);
    auto lib = file.tags.find("_lib");
    if (lib != file.tags.end() && !lib->second.empty())
    {
        File library;
        if (auto err = WalkFile(dir + lib->second, depth + 1, library, walk))
        {
            return err;
        }
    }

    if (auto err = walk.add(path, std::as_const(data), depth, std::as_const(file)))
    {
        return err;
    }

    if (!walk.rules.count_reached)
    {
        walk.files++;
    }

    // The file's _lib2, _lib3 and on load after the file.
    for (int n = 2;; n++)
    {
        auto it = file.tags.find("_lib" + std::to_string(n));
        if (it == file.tags.end())
        {
            break;
        }

        // An empty one names no library. So does an empty _lib.
        if (it->second.empty())
        {
            continue;
        }

        File library;
        if (auto err = WalkFile(dir + it->second, depth + 1, library, walk))
        {
            return err;
        }
    }

    return std::nullopt;
}

} // namespace detail

// Walks the file at `path` and the libraries that its _lib tags name, in loading order: first its _lib with that
// library's chain, then the file itself, then its _lib2, _lib3 and on, each with its chain. An empty _lib or _libN
// names no library. The numbers end before the first one that the file doesn't name. Paths in _lib tags are relative to
// the directory of the file that names them: its path up to the last of `separators`.
//
// `reader` reads each file. A File has the file's tags in `tags`. `parse(path, data, depth, file)` parses the bytes
// `data` of the file at `path` into `file`. `add(path, data, depth, file)` adds the file to the set once the file's
// _lib has loaded. `depth` counts the library tags that led to the file. The file at `path` has depth 0. Its File is
// `top`. Each callback returns an error or nothing. The first error stops the walk. WalkSet returns it.
//
// A walk also stops with an error at a file more than kMaxLibDepth library tags down, at a set of more than
// kMaxSetFiles files (see WalkRules), and at a file that `reader` can't read: "can't read" and the file's path.
template <typename File, typename Parse, typename Add>
std::optional<std::string> WalkSet(const std::string& path, const FileReader& reader, std::string_view separators,
                                   File& top, const Parse& parse, const Add& add, const WalkRules& rules = {})
{
    detail::SetWalk<Parse, Add> walk{reader, separators, parse, add, rules};

    return detail::WalkFile(path, 0, top, walk);
}

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

// Loads the file at `path` and its libraries into `out` (see WalkSet). The set takes its tags from the file at `path`.
template <typename Format>
std::optional<std::string> LoadFiles(const std::string& path, const FileReader& reader, std::string_view separators,
                                     LoadedSet<Format>& out)
{
    // A streaming format reads the program later, straight into its chunks.
    const auto parse = [](const std::string& file_path, const std::vector<uint8_t>& data, int,
                          PsfFile& psf) -> std::optional<std::string>
    {
        if (auto err = ParsePsf(data, psf, !kStreaming<Format>))
        {
            return file_path + ": " + *err;
        }

        if (psf.version != Format::kPsfVersion)
        {
            return file_path + ": not " + AFileOf<Format>() + " (PSF version " + Hex(psf.version, 2) + ")";
        }

        return std::nullopt;
    };

    const auto add = [&out](const std::string& file_path, const std::vector<uint8_t>& data, int,
                            const PsfFile& psf) -> std::optional<std::string>
    {
        ProgramChunks chunks;
        if (auto err = ReadChunks<Format>(file_path, data, psf, chunks))
        {
            return err;
        }

        AddChunks(chunks, out);

        if (auto err = TakeDescriptor(file_path, psf.reserved, out))
        {
            return err;
        }

        out.sources.push_back(file_path);

        return std::nullopt;
    };

    PsfFile top;
    if (auto err = WalkSet(path, reader, separators, top, parse, add))
    {
        return err;
    }

    out.tags = std::move(top.tags);

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

    if (auto err = detail::LoadFiles(path, reader, separators, out))
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
