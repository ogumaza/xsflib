// SPDX-License-Identifier: MIT

// The program of a format that holds each of a set's files once. The program is built from pieces. Some pieces are the
// files themselves. The pieces are compressed as they come. A PSF file's program is read straight into its chunks, and
// inflated as it's read. A large file is then never held twice. The chunks are FILE and SND chunks, laid out as in
// program.h. A path in a FILE chunk or in a descriptor is a 16-bit length and the path's bytes.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "xsflib/psf/program.h"
#include "xsflib/psf/psf.h"

namespace xsflib::psf
{

// Builds a program: the chunks in the order they're added. Each chunk is padded to a multiple of four bytes.
class StreamingProgramBuilder
{
public:
    void AddFile(const std::string& path, const std::vector<uint8_t>& data);

    // Like AddFile, but the program refers to `data` instead of copying it. A large file is then held once. `data` must
    // outlive the builder.
    void AddFileRef(const std::string& path, std::span<const uint8_t> data);

    // Selects the sound to play (`SND ` chunk).
    void AddSound(uint32_t sound_id);

    // Selects several sounds that play together (a `SND ` chunk with a list), in the order in which a player starts
    // them. A list of one sound gives AddSound's chunk. Throws std::invalid_argument for an empty list.
    void AddSounds(std::span<const uint32_t> sound_ids);

    // The program's pieces, in order.
    std::vector<std::span<const uint8_t>> Pieces() const;

    // The whole program.
    std::vector<uint8_t> Data() const;

    // A PSF file holding the program: the bytes that WritePsf makes of Data(). The difference is that this compresses
    // the pieces as they come. The files that the program refers to aren't copied into a whole program first.
    std::vector<uint8_t> WritePsf(uint8_t version, const std::vector<uint8_t>& reserved, const Tags& tags) const;

private:
    // Bytes the builder holds (chunk headers, paths, padding and the files AddFile copies), or bytes it refers to.
    struct Piece
    {
        std::vector<uint8_t> held;
        std::span<const uint8_t> ref;
    };

    void Hold(std::vector<uint8_t> bytes);

    std::vector<Piece> pieces_;
};

// Writes a PSF file whose program is `pieces`, one after another. The pieces are compressed as they come, straight into
// the file's bytes. The bytes are those of WritePsf for the pieces joined. Throws std::runtime_error if zlib fails.
std::vector<uint8_t> WritePsfPieces(uint8_t version, const std::vector<uint8_t>& reserved,
                                    const std::vector<std::span<const uint8_t>>& pieces, const Tags& tags);

// Reads a PSF file's program straight into its FILE and SND chunks: ParsePsf followed by ParseStreamingProgram, but
// without holding the decompressed program. A FILE chunk's data is inflated into its vector with no copy of the whole
// program. Other chunks are skipped.
std::optional<std::string> ParsePsfChunks(const std::vector<uint8_t>& data, ProgramChunks& out);

// Splits a decompressed program into chunks as ParsePsfChunks does: FILE and SND chunks, with others skipped.
std::optional<std::string> ParseStreamingProgram(const std::vector<uint8_t>& program, ProgramChunks& out);

// The fields of the descriptors and of the FILE chunks in both kinds of program: a 16-bit little-endian value, a path
// (its 16-bit length, then its bytes), and zeros to a multiple of four bytes.
void Put16(std::vector<uint8_t>& v, uint32_t x);
void PutPath(std::vector<uint8_t>& v, const std::string& path);
void PadTo4(std::vector<uint8_t>& v);

// Reads a 16-bit value or a path at `pos`, and moves `pos` past it. Returns nothing if `data` ends first.
std::optional<uint32_t> Read16(const std::vector<uint8_t>& data, std::size_t& pos);
std::optional<std::string> ReadPath(const std::vector<uint8_t>& data, std::size_t& pos);

} // namespace xsflib::psf
