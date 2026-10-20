// SPDX-License-Identifier: MIT

// The program of a format that holds each of a set's files once (see streaming.h). The file also holds the parts that
// the streaming program shares with the rest of xsflib::psf: the chunk reader of both kinds of program (chunks.h), the
// inflater of the container's decompressor (inflate.h), the writer of a PSF file in pieces, and the fields of the
// descriptors and of the FILE chunks.

#include "xsflib/psf/streaming.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <zlib.h>

#include "chunks.h"
#include "header.h"
#include "inflate.h"
#include "xsflib/bytes.h"
#include "xsflib/psf/program.h"
#include "xsflib/psf/psf.h"

namespace xsflib::psf
{
namespace
{

// The chunks of a streaming program besides its FILE chunks: a SND chunk, and no MEM chunks.
constexpr detail::ChunkKinds kStreamingChunks{.memory = false, .sound = true};

// A FILE chunk's header and its path, padded to four bytes. The file's data follows them.
std::vector<uint8_t> FileChunkHead(const std::string& path, std::size_t data_size)
{
    std::vector<uint8_t> path_area;
    PutPath(path_area, path);
    PadTo4(path_area);
    std::vector<uint8_t> head = {'F', 'I', 'L', 'E'};
    Put32(head, static_cast<uint32_t>(path_area.size() + data_size));
    head.insert(head.end(), path_area.begin(), path_area.end());

    return head;
}

// A program's bytes in order, read a piece at a time.
class ProgramSource
{
public:
    virtual ~ProgramSource() = default;

    // Reads up to `n` bytes into `dest` and sets `got` to how many it read. Fewer than `n` are read only at the
    // program's end. Returns an error if the program can't be read.
    virtual std::optional<std::string> Read(uint8_t* dest, std::size_t n, std::size_t& got) = 0;

    // The most bytes the program can have left. A chunk's size can then be checked before its data is read.
    virtual uint64_t MaxLeft() const = 0;
};

// A decompressed program.
class MemorySource final : public ProgramSource
{
public:
    explicit MemorySource(const std::vector<uint8_t>& program) : program_(program)
    {
    }

    std::optional<std::string> Read(uint8_t* dest, std::size_t n, std::size_t& got) override
    {
        got = std::min(n, program_.size() - pos_);
        std::copy_n(program_.begin() + static_cast<std::ptrdiff_t>(pos_), got, dest);
        pos_ += got;

        return std::nullopt;
    }

    uint64_t MaxLeft() const override
    {
        return program_.size() - pos_;
    }

private:
    const std::vector<uint8_t>& program_;
    std::size_t pos_ = 0;
};

// A PSF file's compressed program. The program is inflated as it's read. Then a chunk's data can go straight where it
// belongs.
class InflateSource final : public ProgramSource
{
public:
    InflateSource(const uint8_t* compressed, uint32_t size)
    {
        ok_ = inflateInit(&zs_) == Z_OK;
        zs_.next_in = const_cast<uint8_t*>(compressed);
        zs_.avail_in = size;
    }

    ~InflateSource() override
    {
        if (ok_)
        {
            inflateEnd(&zs_);
        }
    }

    InflateSource(const InflateSource&) = delete;
    InflateSource& operator=(const InflateSource&) = delete;

    std::optional<std::string> Read(uint8_t* dest, std::size_t n, std::size_t& got) override
    {
        got = 0;
        if (!error_ && !ok_)
        {
            error_ = "zlib init failed";
        }

        while (!error_ && got < n && !ended_)
        {
            // zlib counts in 32-bit units.
            const std::size_t want = std::min<std::size_t>(n - got, std::size_t{1} << 30);
            zs_.next_out = dest + got;
            zs_.avail_out = static_cast<uInt>(want);
            const int ret = inflate(&zs_, Z_NO_FLUSH);
            const std::size_t produced = want - zs_.avail_out;
            got += produced;
            produced_ += produced;
            if (produced_ > kMaxProgramSize)
            {
                error_ = "program larger than 1 GiB";
            }
            else if (ret == Z_STREAM_END)
            {
                ended_ = true;
            }
            else if (ret == Z_BUF_ERROR && zs_.avail_in == 0)
            {
                error_ = "truncated program data";
            }
            else if (ret != Z_OK)
            {
                error_ = "corrupt program data";
            }
        }

        return error_;
    }

    // Reads the rest of the stream, and returns its error if it has one.
    std::optional<std::string> Drain()
    {
        std::vector<uint8_t> scratch(0x10000);
        std::size_t got = scratch.size();
        while (got == scratch.size())
        {
            if (auto err = Read(scratch.data(), scratch.size(), got))
            {
                return err;
            }
        }

        return std::nullopt;
    }

    uint64_t MaxLeft() const override
    {
        // Deflate makes at most about 1,032 bytes of each compressed byte. zlib can also hold some output that it
        // hasn't given out yet.
        const uint64_t most = (uint64_t{zs_.avail_in} + 1) * 1032 + 0x10000;
        return ended_ ? 0 : std::min<uint64_t>(most, kMaxProgramSize - produced_);
    }

private:
    z_stream zs_{};
    bool ok_ = false;
    bool ended_ = false;
    uint64_t produced_ = 0;
    std::optional<std::string> error_; // once the stream has failed, every read gives its error
};

// The reads inside a chunk, where the program's end is an error.
class ChunkReader
{
public:
    explicit ChunkReader(ProgramSource& source) : source_(source)
    {
    }

    // Reads exactly `n` bytes of a chunk.
    std::optional<std::string> ReadAll(uint8_t* dest, std::size_t n)
    {
        std::size_t got = 0;
        if (auto err = source_.Read(dest, n, got))
        {
            return err;
        }

        return got < n ? std::optional<std::string>("truncated chunk") : std::nullopt;
    }

    // Reads and drops `n` bytes of a chunk.
    std::optional<std::string> Skip(uint64_t n)
    {
        scratch_.resize(static_cast<std::size_t>(std::min<uint64_t>(n, 0x10000)));
        while (n > 0)
        {
            const auto part = static_cast<std::size_t>(std::min<uint64_t>(n, scratch_.size()));
            if (auto err = ReadAll(scratch_.data(), part))
            {
                return err;
            }

            n -= part;
        }

        return std::nullopt;
    }

    // Reads and drops the last `n` bytes of a chunk with a bad header. Returns `error`, or the read's error. A chunk
    // that the program's end cuts short is then reported as truncated, whatever its header.
    std::string Reject(uint64_t n, const char* error)
    {
        return Skip(n).value_or(error);
    }

private:
    ProgramSource& source_;
    std::vector<uint8_t> scratch_;
};

// Reads a MEM chunk of `size` bytes after its header into `out.memory`: the address, then the data.
std::optional<std::string> ReadMemChunk(ChunkReader& reader, uint32_t size, ProgramChunks& out)
{
    if (size < 4)
    {
        return reader.Reject(size, "bad MEM chunk");
    }

    uint8_t address[4];
    if (auto err = reader.ReadAll(address, sizeof(address)))
    {
        return err;
    }

    MemChunk memory;
    memory.address = LoadLe32(address);
    memory.data.resize(size - 4);
    if (auto err = reader.ReadAll(memory.data.data(), memory.data.size()))
    {
        return err;
    }

    out.memory.push_back(std::move(memory));

    return std::nullopt;
}

// Reads a FILE chunk of `size` bytes after its header into `out.files`: the path's length, the path, the padding to
// four bytes, then the data.
std::optional<std::string> ReadFileChunk(ChunkReader& reader, uint32_t size, ProgramChunks& out)
{
    if (size < 2)
    {
        return reader.Reject(size, "bad FILE chunk");
    }

    uint8_t length[2];
    if (auto err = reader.ReadAll(length, sizeof(length)))
    {
        return err;
    }

    const uint32_t len = LoadLe16(length);
    const uint32_t data_start = (2 + len + 3) & ~3u;
    if (data_start > size)
    {
        return reader.Reject(size - 2, "bad FILE chunk");
    }

    FileChunk file;
    file.path.resize(len);
    file.data.resize(size - data_start);
    if (auto err = reader.ReadAll(reinterpret_cast<uint8_t*>(file.path.data()), len))
    {
        return err;
    }
    if (auto err = reader.Skip(data_start - 2 - len))
    {
        return err;
    }
    if (auto err = reader.ReadAll(file.data.data(), file.data.size()))
    {
        return err;
    }

    out.files.push_back(std::move(file));

    return std::nullopt;
}

// Reads a SND chunk of `size` bytes after its header. Its words go into `out.sounds`, in place of an earlier chunk's,
// and the first of them into `out.sound`. A partial word at the end is skipped.
std::optional<std::string> ReadSoundChunk(ChunkReader& reader, uint32_t size, ProgramChunks& out)
{
    if (size < 4)
    {
        return reader.Reject(size, "bad SND chunk");
    }

    std::vector<uint8_t> words(size / 4 * 4);
    if (auto err = reader.ReadAll(words.data(), words.size()))
    {
        return err;
    }
    if (auto err = reader.Skip(size % 4))
    {
        return err;
    }

    out.sounds.clear();
    for (std::size_t at = 0; at < words.size(); at += 4)
    {
        out.sounds.push_back(LoadLe32(&words[at]));
    }

    out.sound = out.sounds.front();

    return std::nullopt;
}

// Reads a chunk of `size` bytes after its header into `out` when it's a FILE chunk or of a kind that `kinds` takes.
// Skips any other chunk.
std::optional<std::string> ReadChunk(ChunkReader& reader, const uint8_t* header, uint32_t size,
                                     detail::ChunkKinds kinds, ProgramChunks& out)
{
    if (kinds.memory && std::memcmp(header, "MEM ", 4) == 0)
    {
        return ReadMemChunk(reader, size, out);
    }

    if (std::memcmp(header, "FILE", 4) == 0)
    {
        return ReadFileChunk(reader, size, out);
    }

    if (kinds.sound && std::memcmp(header, "SND ", 4) == 0)
    {
        return ReadSoundChunk(reader, size, out);
    }

    return reader.Skip(size);
}

// Splits a program into chunks as it reads it (see ParseStreamingProgram): the FILE chunks, and those that `kinds`
// takes. The program may end after any chunk, and in the padding after it.
std::optional<std::string> SplitChunks(ProgramSource& source, ProgramChunks& out, detail::ChunkKinds kinds)
{
    ChunkReader reader(source);
    while (true)
    {
        uint8_t header[8];
        std::size_t got = 0;
        if (auto err = source.Read(header, sizeof(header), got))
        {
            return err;
        }

        // The end. Any bytes left over are too few for a chunk's header.
        if (got < sizeof(header))
        {
            return std::nullopt;
        }

        const uint32_t size = LoadLe32(header + 4);
        if (size > source.MaxLeft())
        {
            return "truncated chunk";
        }

        if (auto err = ReadChunk(reader, header, size, kinds, out))
        {
            return err;
        }

        // The padding to a multiple of four bytes. The program's end may cut it short.
        uint8_t padding[3];
        const std::size_t pad = ((size + 3) & ~3u) - size;
        if (auto err = source.Read(padding, pad, got))
        {
            return err;
        }
        if (got < pad)
        {
            return std::nullopt;
        }
    }
}

} // namespace

std::optional<std::string> detail::ParseChunks(const std::vector<uint8_t>& program, ProgramChunks& out,
                                               ChunkKinds kinds)
{
    MemorySource source(program);
    return SplitChunks(source, out, kinds);
}

std::optional<std::string> detail::InflateWhole(const uint8_t* compressed, uint32_t size, std::vector<uint8_t>& program)
{
    InflateSource source(compressed, size);
    std::vector<uint8_t> buffer(1 << 20);
    std::size_t got = buffer.size();
    while (got == buffer.size())
    {
        if (std::optional<std::string> error = source.Read(buffer.data(), buffer.size(), got))
        {
            return error;
        }

        program.insert(program.end(), buffer.data(), buffer.data() + got);
    }

    return std::nullopt;
}

void Put16(std::vector<uint8_t>& v, uint32_t x)
{
    v.push_back(static_cast<uint8_t>(x));
    v.push_back(static_cast<uint8_t>(x >> 8));
}

// A path in a descriptor or a FILE chunk: its u16 length, then its bytes.
void PutPath(std::vector<uint8_t>& v, const std::string& path)
{
    Put16(v, static_cast<uint32_t>(path.size()));
    v.insert(v.end(), path.begin(), path.end());
}

void PadTo4(std::vector<uint8_t>& v)
{
    while (v.size() % 4)
    {
        v.push_back(0);
    }
}

// Reads a u16 at `pos` and moves past it, or returns nothing if the data ends first.
std::optional<uint32_t> Read16(const std::vector<uint8_t>& data, std::size_t& pos)
{
    if (pos + 2 > data.size())
    {
        return std::nullopt;
    }

    const uint32_t value = LoadLe16(&data[pos]);
    pos += 2;

    return value;
}

// Reads a path in a descriptor at `pos` and moves past it, or returns nothing if the data ends first.
std::optional<std::string> ReadPath(const std::vector<uint8_t>& data, std::size_t& pos)
{
    const std::optional<uint32_t> length = Read16(data, pos);
    if (!length || pos + *length > data.size())
    {
        return std::nullopt;
    }

    std::string path(reinterpret_cast<const char*>(data.data() + pos), *length);
    pos += *length;

    return path;
}

// Writes a PSF file whose program is `pieces`, one after another. The pieces are compressed as they come, straight into
// the file's bytes. From the start, the file's bytes hold room for zlib's bound on the compressed size.
std::vector<uint8_t> WritePsfPieces(uint8_t version, const std::vector<uint8_t>& reserved,
                                    const std::vector<std::span<const uint8_t>>& pieces, const Tags& tags)
{
    uint64_t program_size = 0;
    for (const std::span<const uint8_t> piece : pieces)
    {
        program_size += piece.size();
    }

    const std::string text = FormatTagArea(tags);
    std::vector<uint8_t> out = {'P', 'S', 'F', version};
    Put32(out, static_cast<uint32_t>(reserved.size()));
    Put32(out, 0); // the compressed program's size and CRC, filled in below
    Put32(out, 0);
    out.insert(out.end(), reserved.begin(), reserved.end());
    const std::size_t start = out.size();
    if (program_size > 0)
    {
        z_stream zs{};
        if (deflateInit(&zs, Z_BEST_COMPRESSION) != Z_OK)
        {
            throw std::runtime_error("zlib couldn't compress the program");
        }

        const uLong bound = deflateBound(&zs, static_cast<uLong>(program_size));
        out.reserve(start + bound + text.size());
        out.resize(start + bound);
        zs.next_out = out.data() + start;
        zs.avail_out = static_cast<uInt>(bound);
        bool ok = true;
        for (const std::span<const uint8_t> piece : pieces)
        {
            // zlib counts in 32-bit units.
            for (std::size_t done = 0; ok && done < piece.size();)
            {
                const std::size_t part = std::min<std::size_t>(piece.size() - done, std::size_t{1} << 30);
                zs.next_in = const_cast<uint8_t*>(piece.data() + done);
                zs.avail_in = static_cast<uInt>(part);
                ok = deflate(&zs, Z_NO_FLUSH) == Z_OK && zs.avail_in == 0;
                done += part;
            }
        }

        ok = ok && deflate(&zs, Z_FINISH) == Z_STREAM_END;
        const std::size_t compressed = bound - zs.avail_out;
        deflateEnd(&zs);
        if (!ok)
        {
            throw std::runtime_error("zlib couldn't compress the program");
        }

        out.resize(start + compressed);
        StoreLe32(&out[8], static_cast<uint32_t>(compressed));
        StoreLe32(&out[12], static_cast<uint32_t>(crc32(0, out.data() + start, static_cast<uInt>(compressed))));
    }

    out.insert(out.end(), text.begin(), text.end());

    return out;
}

void StreamingProgramBuilder::Hold(std::vector<uint8_t> bytes)
{
    if (!bytes.empty())
    {
        pieces_.push_back({std::move(bytes), {}});
    }
}

void StreamingProgramBuilder::AddFile(const std::string& path, const std::vector<uint8_t>& data)
{
    std::vector<uint8_t> chunk = FileChunkHead(path, data.size());
    chunk.insert(chunk.end(), data.begin(), data.end());
    PadTo4(chunk);
    Hold(std::move(chunk));
}

void StreamingProgramBuilder::AddFileRef(const std::string& path, std::span<const uint8_t> data)
{
    Hold(FileChunkHead(path, data.size()));
    pieces_.push_back({{}, data});
    Hold(std::vector<uint8_t>((4 - data.size() % 4) % 4, 0));
}

void StreamingProgramBuilder::AddSound(uint32_t sound_id)
{
    const uint32_t sound_ids[] = {sound_id};
    AddSounds(sound_ids);
}

void StreamingProgramBuilder::AddSounds(std::span<const uint32_t> sound_ids)
{
    if (sound_ids.empty())
    {
        throw std::invalid_argument("a SND chunk selects at least one sound");
    }

    std::vector<uint8_t> chunk = {'S', 'N', 'D', ' '};
    Put32(chunk, static_cast<uint32_t>(sound_ids.size() * 4));
    for (const uint32_t sound_id : sound_ids)
    {
        Put32(chunk, sound_id);
    }

    Hold(std::move(chunk));
}

std::vector<std::span<const uint8_t>> StreamingProgramBuilder::Pieces() const
{
    std::vector<std::span<const uint8_t>> pieces;
    for (const Piece& piece : pieces_)
    {
        pieces.push_back(piece.held.empty() ? piece.ref : std::span<const uint8_t>(piece.held));
    }

    return pieces;
}

std::vector<uint8_t> StreamingProgramBuilder::Data() const
{
    std::vector<uint8_t> program;
    for (const std::span<const uint8_t> piece : Pieces())
    {
        program.insert(program.end(), piece.begin(), piece.end());
    }

    return program;
}

std::vector<uint8_t> StreamingProgramBuilder::WritePsf(uint8_t version, const std::vector<uint8_t>& reserved,
                                                       const Tags& tags) const
{
    return WritePsfPieces(version, reserved, Pieces(), tags);
}

std::optional<std::string> ParseStreamingProgram(const std::vector<uint8_t>& program, ProgramChunks& out)
{
    return detail::ParseChunks(program, out, kStreamingChunks);
}

std::optional<std::string> ParsePsfChunks(const std::vector<uint8_t>& data, ProgramChunks& out)
{
    const std::optional<detail::PsfHeader> header = detail::ReadPsfHeader(data);
    if (!header)
    {
        return "not a PSF file";
    }

    const uint32_t reserved_size = header->reserved_size;
    const uint32_t program_size = header->program_size;
    const uint32_t crc = header->crc;
    if (!header->complete)
    {
        return "truncated PSF file";
    }
    if (program_size == 0)
    {
        return std::nullopt;
    }

    const uint8_t* compressed = data.data() + 16 + reserved_size;
    if (crc32(0, compressed, program_size) != crc)
    {
        return "program CRC mismatch";
    }

    // An error in the zlib stream comes first. The same happens when the program is inflated whole before it's split.
    InflateSource source(compressed, program_size);
    const auto err = SplitChunks(source, out, kStreamingChunks);
    if (err)
    {
        if (auto stream_err = source.Drain())
        {
            return stream_err;
        }
    }

    return err;
}

} // namespace xsflib::psf
